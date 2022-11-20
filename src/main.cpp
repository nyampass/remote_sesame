/*
 * libasesame3btサンプル
 * SesameのBluetoothアドレスがわかっている場合
 */
#include <M5StickCPlus.h>
#include <Sesame.h>
#include <SesameClient.h>
#include <SesameScanner.h>
#include <WiFi.h>
// Sesame鍵情報設定用インクルードファイル
// 数行下で SESAME_SECRET 等を直接定義する場合は別ファイルを用意する必要はない
#if __has_include("mysesame-config.h")
#include "mysesame-config.h"
#endif

// 64 bytes public key of Sesame
// 128 bytes hex str
const char *sesame_pk = SESAME_PK;
// 16 bytes secret key of Sesame
// 32 bytes hex str
const char *sesame_sec = SESAME_SECRET;

const char *ssid = SSID;
const char *password = PASSWORD;
const char *host = HOST;

using libsesame3bt::Sesame;
using libsesame3bt::SesameClient;
using libsesame3bt::SesameInfo;
using libsesame3bt::SesameScanner;

SesameClient client{};
SesameClient::Status last_status{};
SesameClient::state_t sesame_state;

// Sesameの状態通知コールバック
// Sesameのつまみの位置、電圧、施錠開錠状態が通知される
// Sesameからの通知がある毎に呼び出される(変化がある場合のみ通知されている模様)
// Sesameの設定変更があった場合も呼び出される(はずだが、今のところ動作していない)
void status_update(SesameClient &client, SesameClient::Status status)
{
	if (status != last_status)
	{
		Serial.printf_P(PSTR("Setting lock=%d,unlock=%d\n"), status.lock_position(), status.unlock_position());
		Serial.printf_P(PSTR("Status in_lock=%u,in_unlock=%u,pos=%d,volt=%.2f,volt_crit=%u\n"), status.in_lock(), status.in_unlock(),
						status.position(), status.voltage(), status.voltage_critical());
		last_status = status;
	}
}

static const char *
model_str(Sesame::model_t model)
{
	switch (model)
	{
	case Sesame::model_t::sesame_3:
		return "SESAME 3";
	case Sesame::model_t::wifi_2:
		return "Wi-Fi Module 2";
	case Sesame::model_t::sesame_bot:
		return "SESAME bot";
	case Sesame::model_t::sesame_cycle:
		return "SESAME Cycle";
	case Sesame::model_t::sesame_4:
		return "SESAME 4";
	default:
		return "UNKNOWN";
	}
}

const SesameInfo
	*
	scan_and_init()
{
	// SesameScannerはシングルトン
	SesameScanner &scanner = SesameScanner::get();

	Serial.println(F("Scanning 10 seconds"));
	const SesameInfo *result;
	scanner.scan(10, [&result](SesameScanner &_scanner, const SesameInfo *_info)
				 {
		if (_info) {  // nullptrの検査を実施
			Serial.printf_P(PSTR("model=%s,addr=%s,UUID=%s,registered=%u\n"), model_str(_info->model), _info->address.toString().c_str(),
			                _info->uuid.toString().c_str(), _info->flags.registered);
			if(_info->uuid.toString() == UUID){
				Serial.println(F("#################device found#################"));
				result = _info;
				_scanner.stop(); // スキャンを停止させたくなったらstop()を呼び出す
			}
		} });
	if (result)
	{
		return result;
	}
	else
	{
		Serial.println(F("No usable Sesame found"));
		return nullptr;
	}
}

WiFiClient wifiClient;
const int httpPort = HTTPPORT;
String fetchStatus()
{
	if (!wifiClient.connect(host, httpPort))
	{
		Serial.println("connection failed");
		return "";
	}

	// This will send the request to the server
	wifiClient.print(String("GET ") + "/get-status" + " HTTP/1.1\r\n" +
					 "Host: " + host + "\r\n" +
					 "Connection: close\r\n\r\n");
	unsigned long timeout = millis();
	while (wifiClient.available() == 0)
	{
		if (millis() - timeout > 5000)
		{
			Serial.println(">>> Client Timeout !");
			wifiClient.stop();
			return "";
		}
	}

	// Read all the lines of the reply from server and print them to Serial
	String status = "";
	while (wifiClient.available())
	{
		status = wifiClient.readStringUntil('\r');
	}
	status.trim();

	Serial.println("Server Status : " + status);
	Serial.println("closing connection");
	Serial.println();

	return status;
}

String serverStatus = "999";
void setup()
{
	pinMode(10, OUTPUT);
	digitalWrite(10, HIGH);

	Serial.begin(115200);

	delay(10);

	// We start by connecting to a WiFi network

	Serial.println();
	Serial.println();
	Serial.print("Connecting to ");
	Serial.println(ssid);

	WiFi.begin(ssid, password);

	while (WiFi.status() != WL_CONNECTED)
	{
		delay(500);
		Serial.print(".");
	}

	Serial.println("");
	Serial.println("WiFi connected");
	Serial.println("IP address: ");
	Serial.println(WiFi.localIP());

	// Bluetoothは初期化しておくこと
	BLEDevice::init("");

	auto info = scan_and_init();
	if (info == nullptr)
	{
		Serial.println(F("Failed to begin"));
		return;
	}

	// Bluetoothアドレスと機種コードを設定(sesame_3, sesame_4, sesame_cycle を指定可能)
	if (!client.begin(info->address, info->model))
	{
		Serial.println(F("Failed to begin"));
		return;
	}
	// Sesameの鍵情報を設定
	if (!client.set_keys(sesame_pk, sesame_sec))
	{
		Serial.println(F("Failed to set keys"));
		return;
	}
	// SesameClient状態コールバックを設定
	client.set_state_callback([](auto &client, auto state)
							  { sesame_state = state; });
	// Sesame状態コールバックを設定
	// (SESAME botは異なる呼び出しが必要。by_address_botを参照)
	client.set_status_callback(status_update);
	serverStatus = fetchStatus();
}

static uint32_t last_operated = 0;
int state = 0;
int count = 0;

void loop()
{
	// 接続開始、認証完了待ち、開錠、施錠を順次実行する
	String _fetchStatu = "999";
	switch (state)
	{
	case 0:
		if (last_operated == 0 || millis() - last_operated > 3000)
		{
			count++;
			Serial.println(F("Connecting..."));
			// connectはたまに失敗するようなので3回リトライする
			if (!client.connect(3))
			{
				Serial.println(F("Failed to connect, abort"));
				state = 4;
				return;
			}
			Serial.println(F("connected"));
			last_operated = millis();
			state = 1;
		}
		break;
	case 1:
		digitalWrite(10, LOW);
		_fetchStatu = fetchStatus();
		if (serverStatus == _fetchStatu)
			break;
		serverStatus = _fetchStatu;
		Serial.println("######Status######");

		if (client.is_session_active())
		{
			// Serial.println(F("Unlocking"));
			// unloc(), lock()ともにコマンドの送信が成功した時点でtrueを返す
			// 開錠、施錠されたかはstatusコールバックで確認する必要がある
			Serial.println("##" + serverStatus + "##");
			if (serverStatus == "1")
			{
				if (!client.unlock(u8"開錠:テスト"))
				{
					Serial.println(F("Failed to send unlock command"));
				}
			}
			if (serverStatus == "0")
			{
				Serial.println(F("Locking"));
				if (!client.lock(u8"施錠:テスト"))
				{
					Serial.println(F("Failed to send lock command"));
				}
			}
			if (serverStatus == "2")
			{
				Serial.println("none");
				state = 3;
			}
			last_operated = millis();
		}
		else
		{
			if (client.get_state() == SesameClient::state_t::idle)
			{
				Serial.println(F("Failed to authenticate"));
				state = 4;
			}
		}
		break;
	case 3:
		if (millis() - last_operated > 3000)
		{
			client.disconnect();
			Serial.println(F("Disconnected"));
			last_operated = millis();
			if (count > 0)
			{
				state = 4;
			}
			else
			{
				state = 0;
			}
		}
		break;
	case 4:
		// テストを兼ねてデストラクタを呼び出しているが、あえて明示的に呼び出す必要はない
		client.~SesameClient();
		Serial.println(F("All done"));
		digitalWrite(10, HIGH);
		state = 9999;
		break;
	default:
		// nothing todo
		break;
	}
	delay(100);
}
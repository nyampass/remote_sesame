/*
 * libasesame3btサンプル
 * SesameのBluetoothアドレスがわかっている場合
 */
#include <M5StickCPlus.h>
#include <Sesame.h>
#include <SesameClient.h>
#include <SesameScanner.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// 設定用インクルードファイル
#if __has_include("config.h")
#include "config.h"
#endif

#define HTTPS_PORT 443

// 64 bytes public key of Sesame
// 128 bytes hex str
const char *sesame_pk = SESAME_PK;
// 16 bytes secret key of Sesame
// 32 bytes hex str
const char *sesame_sec = SESAME_SECRET;

const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

const char *api_server = API_SERVER;
const char *api_path = API_PATH;
const char *api_key = API_KEY;

using libsesame3bt::Sesame;
using libsesame3bt::SesameClient;
using libsesame3bt::SesameInfo;
using libsesame3bt::SesameScanner;

SesameClient client{};
SesameClient::Status last_status{};
SesameClient::state_t sesame_state;

enum Status
{
	Status_none,
	Status_requestLock,
	Status_requestUnlock,
	Status_requestStatus,
	Status_fetchFailed,
};

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

Status fetchStatus()
{
#if API_PORT == HTTPS_PORT
	WiFiClientSecure client;
	webClient.setInsecure(); // skip verification
#else
	WiFiClient client;
#endif // API_PORT == HTTPS_PORT

	if (!client.connect(api_server, API_PORT))
	{
		Serial.println("Connection failed!");
		return Status_fetchFailed;
	}

	client.println(String("GET https://") + api_server + api_path + " HTTP/1.0\r\n" +
				   "Host: " + api_server + "\r\n" +
				   "Connection: close\r\n");

	unsigned long timeout = millis();
	while (client.available() == 0)
	{
		if (millis() - timeout > 5000)
		{
			client.stop();
			return Status_fetchFailed;
		}
	}

	while (client.available())
	{
		String line = client.readStringUntil('\n');
		if (line == "\r")
		{
			String res = client.readStringUntil('\n');
			client.stop();

			// M5.Lcd.printf(res.c_str());
			if (res == "lock")
			{
				return Status_requestLock;
			}
			else if (res == "unlock")
			{
				return Status_requestUnlock;
			}
			else if (res == "status")
			{
				return Status_requestStatus;
			}

			return Status_none;
		}
	}

	return Status_none;
}

void setup()
{
	M5.begin();
	M5.Lcd.setRotation(1);
	M5.Lcd.setTextSize(2);

	pinMode(10, OUTPUT);
	digitalWrite(10, HIGH);

	Serial.begin(115200);

	delay(10);

	M5.Lcd.print("Network: ");

	WiFi.begin(ssid, password);

	while (WiFi.status() != WL_CONNECTED)
	{
		delay(500);
		M5.Lcd.print(".");
	}

	M5.Lcd.println("ok!");

	// Serial.println("WiFi connected");
	// Serial.println("IP address: ");
	// Serial.println(WiFi.localIP());

	// Bluetoothは初期化しておくこと
	BLEDevice::init("");

	M5.Lcd.print("Key: ");

	auto info = scan_and_init();
	if (info == nullptr)
	{
		Serial.println(F("Failed to begin"));
		M5.Lcd.println("failed");
		return;
	}

	// Bluetoothアドレスと機種コードを設定(sesame_3, sesame_4, sesame_cycle を指定可能)
	if (!client.begin(info->address, info->model))
	{
		Serial.println(F("Failed to begin"));
		M5.Lcd.println("failed");
		return;
	}
	// Sesameの鍵情報を設定
	if (!client.set_keys(sesame_pk, sesame_sec))
	{
		Serial.println(F("Failed to set keys"));
		M5.Lcd.println("failed");
		return;
	}
	// SesameClient状態コールバックを設定
	client.set_state_callback([](auto &client, auto state)
							  { sesame_state = state; });
	// Sesame状態コールバックを設定
	// (SESAME botは異なる呼び出しが必要。by_address_botを参照)
	client.set_status_callback(status_update);

	M5.Lcd.println("clear!");
}

static uint32_t last_operated = 0;
int count = 0;

void loop()
{
	// M5.Lcd.fillScreen(BLACK);
	// M5.Lcd.setCursor(0, 0);

	Status status = fetchStatus();
	M5.Lcd.print(status);

	if (status != Status_none && status != Status_fetchFailed)
	{
		if (!client.connect(3))
		{
			M5.Lcd.print("F");
		}
		while (!client.is_session_active())
		{
			M5.Lcd.print(".");
			delay(500);
		}
		switch (status)
		{
		case Status_requestLock:
			client.lock(u8"施錠:テスト");
			break;
		case Status_requestUnlock:
			client.unlock(u8"開錠:テスト");
			break;
		case Status_requestStatus:
			// Serial.println((uint8_t)client.get_state());
			M5.Lcd.print("->");
			M5.Lcd.print((uint8_t)client.get_state());
			break;
		}
		client.disconnect();
	}

	// switch (state)
	// {
	// case 0:
	// 	if (last_operated == 0 || millis() - last_operated > 3000)
	// 	{
	// 		count++;
	// 		Serial.println(F("Connecting..."));
	// 		// connectはたまに失敗するようなので3回リトライする
	// 		if (!client.connect(3))
	// 		{
	// 			Serial.println(F("Failed to connect, abort"));
	// 			state = 4;
	// 			return;
	// 		}
	// 		Serial.println(F("connected"));
	// 		last_operated = millis();
	// 		state = 1;
	// 	}
	// 	break;
	// case 1:
	// 	digitalWrite(10, LOW);
	// 	_fetchStatu = fetchStatus();
	// 	if (serverStatus == _fetchStatu)
	// 		break;
	// 	serverStatus = _fetchStatu;
	// 	Serial.println("######Status######");

	// 	if (client.is_session_active())
	// 	{
	// 		// Serial.println(F("Unlocking"));
	// 		// unloc(), lock()ともにコマンドの送信が成功した時点でtrueを返す
	// 		// 開錠、施錠されたかはstatusコールバックで確認する必要がある
	// 		Serial.println("##" + serverStatus + "##");
	// 		if (serverStatus == "1")
	// 		{
	// 			if (!client.unlock(u8"開錠:テスト"))
	// 			{
	// 				Serial.println(F("Failed to send unlock command"));
	// 			}
	// 		}
	// 		if (serverStatus == "0")
	// 		{
	// 			Serial.println(F("Locking"));
	// 			if (!client.lock(u8"施錠:テスト"))
	// 			{
	// 				Serial.println(F("Failed to send lock command"));
	// 			}
	// 		}
	// 		if (serverStatus == "2")
	// 		{
	// 			Serial.println("none");
	// 			state = 3;
	// 		}
	// 		last_operated = millis();
	// 	}
	// 	else
	// 	{
	// 		if (client.get_state() == SesameClient::state_t::idle)
	// 		{
	// 			Serial.println(F("Failed to authenticate"));
	// 			state = 4;
	// 		}
	// 	}
	// 	break;
	// case 3:
	// 	if (millis() - last_operated > 3000)
	// 	{
	// 		client.disconnect();
	// 		Serial.println(F("Disconnected"));
	// 		last_operated = millis();
	// 		if (count > 0)
	// 		{
	// 			state = 4;
	// 		}
	// 		else
	// 		{
	// 			state = 0;
	// 		}
	// 	}
	// 	break;
	// case 4:
	// 	// テストを兼ねてデストラクタを呼び出しているが、あえて明示的に呼び出す必要はない
	// 	client.~SesameClient();
	// 	Serial.println(F("All done"));
	// 	digitalWrite(10, HIGH);
	// 	state = 9999;
	// 	break;
	// default:
	// 	// nothing todo
	// 	break;
	// }
	delay(5000);
}
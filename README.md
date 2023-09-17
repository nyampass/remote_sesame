# sesame 5 の仕様

```
Secret Key: Sesame3,4と同様
Public Key: UUIDとして使用、またconfig.hのSESAME_PKにはnullptrを代入（文字列ではなくそのまま）
```

### 例

```c++
#define SESAME_PK nullptr
#define UUID "QRコードから読み取れる情報のPublic Key部分"
```

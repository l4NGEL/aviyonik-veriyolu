# aviyonik-veriyolu

MIL-STD-1553B ve CAN için C++17 veri yolu kütüphanesi. Yanında, FreeRTOS üzerinde
çalışan ve 1553'ten gelen telemetriyi CAN'e aktaran bir gerçek zamanlı örnek uygulama var.

Kütüphane kararlı durumda dinamik bellek ayırmaz. Her alt seviye gereksinim (`LLR-*`)
kodda etiketlidir ve en az bir testle doğrulanır.

## İçerik

**MIL-STD-1553B** (`include/avb/m1553`)

- Kelime biçimleri: komut, durum ve veri kelimeleri, tek eşlik, mod kodları.
- Manchester II bi-faz kodlama ve çözme: kelime başına 40 yarım bit, senkron alanı ve ihlal tespiti.
- Çift yedekli (A/B) veri yolunun ayrık zamanlı modeli: benzetim saati, RT yanıt süreleri, hat boşluğu.
- Uzak terminal (RT): alt adres tamponları, durum yazmacı, yayın, RT→RT aktarımı, gecikme/susma senaryoları.
- Veri yolu denetleyicisi (BC): yanıt doğrulama, yanıt gelmezse yedek hatta yeniden deneme,
  küçük/büyük çerçeve zamanlayıcısı ve en kötü durum doluluk hesabı.
- Hata enjektörü (yarım bit çevirme, kelime düşürme, rastgele bit hatası) ve halka tamponlu veri yolu izleyicisi.

**CAN** (`include/avb/can`)

- DBC tarzı sinyal paketleme: Intel/Motorola bayt sırası, işaret genişletme, ölçek/ofset, aralık kontrolü.
- ISO 15765-2 (ISO-TP): ilk/ardışık çerçeve, akış kontrolü, blok boyu, STmin, zaman aşımları.
  Taşımadan bağımsız durum makinesi olarak yazıldı, 4095 baytlık sabit tampon kullanır.
- Linux SocketCAN (`CAN_RAW`) için RAII sarmalayıcı: `poll()` ile zaman aşımlı alım, çekirdek düzeyinde kimlik filtresi.

**FreeRTOS örneği** (`rtos/`): FreeRTOS-Kernel V11.1.0, POSIX portu üzerinde

| Görev | Öncelik | Periyot | İş |
|---|---|---|---|
| VeriYolu | 4 | 20 ms | BC küçük çerçevesini yürütür (INS → gösterge, RT→RT), veriyi bütünlük kontrolüyle kuyruğa koyar |
| Sensör | 3 | 10 ms | INS RT'sinin alt adres verisini günceller |
| CAN | 2 | kuyruk | Telemetriyi CAN sinyallerine paketler, `vcan0`'a yazar |
| İzleyici | 1 | 50 ms | `vcan0`'dan geri okur, süre sonunda ölçütleri değerlendirir |

RT nesneleri iki görev arasında paylaşıldığı için bir mutex ile korunur. `--oncelik` kipi aynı
kilitleme senaryosunu önce ikili semaforla, sonra mutex ile koşturur ve öncelik tersinmesini ölçer.

## Sonuçlar

Aşağıdaki değerler Docker imajında (Ubuntu 24.04, GCC 14) alındı.

- **Testler:** 80/80 geçiyor (78 GoogleTest/ctest + 2 RTOS senaryosu). Release, Debug ve
  ASan+UBSan derlemelerinin hepsi `-Werror` ile temiz. ASan derlemesinde FreeRTOS örneği hariç tutuluyor.
- **Dinamik bellek:** `sifir_dinamik_bellek` testi global `operator new`'u sayıyor. Kurulumdan sonraki
  1000 küçük çerçeve boyunca (BC, RT, izleyici ve ISO-TP yolları) hiç ayırma yapılmıyor.
- **Kapsama:** `src/` altında satır kapsaması yaklaşık %90 (gcov, `-DAVB_COVERAGE=ON`). En düşük dosya
  `socketcan.cpp` (%71), çünkü çekirdek hata yolları testte tetiklenmiyor.
- **RTOS, 5 sn:** 500 sensör ve 250 veri yolu turunda son tarih kaçırılmadı. 625 adet 1553 mesajında
  0 hata, 0 yırtık veri oluştu. CAN tarafında 250 çerçeve gönderildi, 250'si geri okundu.
- **Öncelik tersinmesi:** yüksek öncelikli görevin kilit beklemesi ikili semaforla 105 ms, mutex
  (öncelik kalıtımı) ile 25 ms.
- **Statik analiz:** `cppcheck` (warning, performance, portability) bulgu vermiyor.

## Derleme ve test

```bash
docker build -t aviyonik-veriyolu:dev .
docker run --rm --cap-add NET_ADMIN -v "$PWD:/src" aviyonik-veriyolu:dev scripts/docker-test.sh
```

`NET_ADMIN` yetkisi `vcan0` arabirimini kurmak için gerekiyor. Bu yetki yoksa SocketCAN testleri atlanır.

Seçenekler:

```bash
# ASan + UBSan
scripts/docker-test.sh -DCMAKE_BUILD_TYPE=Debug -DAVB_BUILD_RTOS=OFF -DAVB_SANITIZE=address,undefined
# Satır/dal/MC-DC kapsaması (GCC 14+)
scripts/docker-test.sh -DAVB_COVERAGE=ON -DAVB_BUILD_RTOS=OFF
```

Örnek uygulamalar:

- `m1553_demo`: A hattına hata enjekte edilmiş bir çizelgede BC'nin B hattında kurtarmasını veri yolu izleyicisi çıktısı olarak gösterir.
- `rtos_terminal --sure 5`: gerçek zamanlı görev senaryosu.
- `rtos_terminal --oncelik`: öncelik tersinmesi deneyi.

## Sınırlar

- 1553 tarafı donanım değil, bir **benzetim**dir. Zamanlama sabitleri standarda göre seçildi ama
  gerçek bir transceiver ya da kart üzerinde denenmedi.
- FreeRTOS **POSIX portu** üzerinde çalışıyor. Görevler Linux iş parçacıkları olduğundan ölçülen periyot
  sapmaları (ms mertebesinde) hedef donanımdaki gerçek zamanlı davranışı temsil etmez.
- CAN yalnızca sanal arabirimle (`vcan0`) denendi. CAN FD desteklenmiyor.

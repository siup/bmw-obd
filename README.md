# BMW OBD Dashboard (Arduino IDE)

Ten projekt przedstawia szkic Arduino IDE dla ESP32, który łączy się z modułem ELM327 (Bluetooth SPP)
i prezentuje podstawowe dane OBD-II na wyświetlaczu TFT 3.2" ILI9341 z panelem dotykowym XPT2046.

## Struktura katalogów

```
arduino/
  BMWE92_OBD_Dashboard/
    BMWE92_OBD_Dashboard.ino
```

W Arduino IDE otwórz folder `arduino/BMWE92_OBD_Dashboard` jako szkic.

## Sprzęt
- ESP32-WROOM / ESP32-S3 (rdzeń ESP32 dla Arduino)
- Wyświetlacz TFT ILI9341 320x240 (SPI)
- Panel dotykowy XPT2046 (SPI)
- Adapter OBD-II ELM327 Bluetooth Classic (SPP)
- Zasilanie 12 V -> przetwornica buck 5 V (minimum 2 A)

## Biblioteki
Zainstaluj w Arduino IDE (Library Manager):
- Adafruit GFX Library
- Adafruit ILI9341
- XPT2046_Touchscreen (Paul Stoffregen)

Biblioteka `BluetoothSerial` jest dostępna po zainstalowaniu oficjalnego rdzenia ESP32.

## Konfiguracja
W pliku `.ino` ustaw:
- `ELM_BT_NAME` – nazwę modułu ELM327 do parowania
- Piny CS/DC/RST dla wyświetlacza oraz CS/IRQ dla panelu dotykowego (zgodnie z Twoim podłączeniem)
- Dodatkowe PID-y producenta możesz dodać w sekcji komentarza na końcu pliku

## Uruchomienie
1. W Arduino IDE wybierz płytkę ESP32 (np. "ESP32 Dev Module").
2. Sparuj moduł ELM327 z komputerem, aby Arduino IDE mogło go wykryć przy pierwszym uruchomieniu.
3. Wgraj szkic na ESP32.
4. Po starcie ESP32 spróbuje połączyć się z ELM327 i zacznie odświeżać dane.

## Licencja
Projekt udostępniony "as-is" w celach edukacyjnych.

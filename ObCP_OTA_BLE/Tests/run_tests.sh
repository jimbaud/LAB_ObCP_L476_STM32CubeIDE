#!/bin/sh
# Lance tous les tests sur PC (aucune carte nécessaire).
#   0. compilation des deux projets STM32 comme STM32CubeIDE (Debug), si le
#      compilateur arm-none-eabi-gcc est installé ; sinon, les tests Python
#      utilisent les programmes déjà compilés du dossier Binaires ;
#   1. tests unitaires C du chargeur (AddressSanitizer + UBSan) : le cœur du
#      transfert (ota.c), puis la partie Bluetooth (ble_ota.c) avec un faux
#      module BlueNRG ;
#   2. bibliothèque partagée libota_sim.so pour les tests Python ;
#   3. tests Python : lecture des fichiers, cohérence C/Python, simulation
#      de bout en bout entre obcp_ota (PC) et le vrai ota.c (chargeur).
set -e
ICI=$(cd "$(dirname "$0")" && pwd)
LOADER="$ICI/../ObCP_BLE_Loader/Loader"
mkdir -p "$ICI/build/sim"
SRC="$LOADER/Src/ota.c $LOADER/Src/crc32.c $LOADER/Src/app_image.c $ICI/sim/fake_app_flash.c"
CFLAGS="-std=c11 -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -I$LOADER/Inc -I$ICI/sim"

echo "=== 0. Compilation des projets STM32 (Debug) ==="
if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    python3 "$ICI/build_arm.py" "$ICI/../ObCP_BLE_Loader"
    python3 "$ICI/build_arm.py" "$ICI/../ObCP_OTA_Demo"
else
    echo "arm-none-eabi-gcc introuvable : les tests utiliseront le dossier Binaires"
fi

echo "=== 1. Tests unitaires C (ASan + UBSan) ==="
gcc $CFLAGS -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
    $SRC "$ICI/sim/test_ota.c" -o "$ICI/build/sim/test_ota"
"$ICI/build/sim/test_ota"
BLE="$ICI/../ObCP_BLE_Loader"
INC_BLE="-I$ICI/sim/hote -I$ICI/sim -I$LOADER/Inc -I$BLE/Middlewares/ST/BlueNRG-MS/includes \
 -I$BLE/Middlewares/ST/BlueNRG-MS/hci/hci_tl_patterns/Basic -I$BLE/Middlewares/ST/BlueNRG-MS/utils \
 -I$BLE/BlueNRG-MS/Target"
gcc -std=c11 -Wall -Wextra -Wshadow -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
    $INC_BLE "$LOADER/Src/ble_ota.c" $SRC "$ICI/sim/test_ble_ota.c" -o "$ICI/build/sim/test_ble_ota"
"$ICI/build/sim/test_ble_ota"

echo "=== 2. Bibliothèque de simulation ==="
gcc $CFLAGS -O2 -shared -fPIC $SRC -o "$ICI/build/sim/libota_sim.so"
echo "ok : $ICI/build/sim/libota_sim.so"

echo "=== 3. Tests Python ==="
cd "$ICI"
python3 -m unittest -v test_outil_pc 2>&1

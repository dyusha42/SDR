#!/usr/bin/env bash
# автотест без железа: передатчик -> канал -> приёмник
set -u
TX="$1"; SIM="$2"; RX="$3"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
fail=0

# аргументы: модуляция, текст, CFO, SNR, задержка, DC, мусор в начале (буферов), --fill (0/1), ожидаемый текст
run_case() {
  local mod="$1" msg="$2" cfo="$3" snr="$4" delay="$5" dc="$6" lead="$7" fill="${8:-0}" want="${9:-$2}"
  local fa=""; [ "$fill" = "1" ] && fa="--fill"
  "$TX" --mod "$mod" --msg "$msg" $fa --dump "$TMP/d" >/dev/null || { echo "FAIL: lab3_tx --dump"; fail=1; return; }
  "$SIM" --in "$TMP/d/tx_burst.pcm" --out "$TMP/rx.pcm" --buffers 80 --every 10 --cfo "$cfo" --snr "$snr" --delay "$delay" --dc "$dc" --lead "$lead" --phase 2.1 >/dev/null || { echo "FAIL: channel_sim"; fail=1; return; }
  if (cd "$TMP" && "$RX" --mod "$mod" --file rx.pcm --expect "$want" >out.txt) && [ -s "$TMP/frames.csv" ] && [ -s "$TMP/mf_out.pcm" ]; then
    local n; n=$(grep -c "CRC OK" "$TMP/out.txt")
    echo "OK   mod=$mod cfo=${cfo}Hz snr=${snr}dB delay=$delay dc=$dc lead=$lead -> кадров: $n"
  else
    echo "FAIL mod=$mod cfo=${cfo}Hz snr=${snr}dB delay=$delay dc=$dc lead=$lead"; cat "$TMP/out.txt"; fail=1
  fi
}

run_case qpsk "sss" 0      20 0    0  0
run_case qpsk "sss" 8000   15 357  0  0
run_case qpsk "sss" -15000 15 1500 2  0
run_case qpsk "Hello, SDR! Лаб.4 ok" 20000 12 999 0 0
run_case qpsk "sss" 100    18 700  0  3
run_case bpsk "sss" 8000   15 357  0  0
run_case bpsk "Hi!"             -20000 12 1111 -2 0
run_case bpsk "0123456789ABCDEF" 3000  18 1919 0  2
# режим --fill: сообщение повторяется через пробел до предела буфера (36 символов qpsk, 16 bpsk)
tile() {
  local t="$1" n="$2" out=""
  while [ ${#out} -lt "$n" ]; do [ -n "$out" ] && out="$out "; out="$out$t"; done
  echo "${out:0:$n}"
}
run_case qpsk "ab" 5000 15 400 0 0 1 "$(tile ab 36)"
run_case bpsk "xyz" -5000 15 800 0 0 1 "$(tile xyz 16)"
exit $fail

# pico2vibe (RP2350 Zero)

Projeto pronto para compilar com Pico SDK 2.x para RP2350.

## Pré-requisitos

- `cmake` 3.13+
- `ninja`
- `arm-none-eabi-gcc`
- variável `PICO_SDK_PATH` apontando para o Pico SDK (ex.: `C:\Users\devx\.pico-sdk\sdk\2.2.0`)

## Compilação

```powershell
cmake --preset rp2350-zero
cmake --build --preset rp2350-zero -j
```

## Artefato para gravação

Arquivo UF2 gerado em:

`build/rp2350-zero/univibe_rp2350_dma.uf2`

## Pins usados (I2S + MCLK)

- `MCLK`: GPIO21
- `BCLK`: GPIO16
- `LRCLK`: GPIO17
- `DOUT`: GPIO18
- `DIN`: GPIO19
# Desktop Tools (CLI + GUI)

Esta pasta adiciona um fluxo de teste offline no PC sem mexer no firmware do Pico.

## Objetivo

- Reusar automaticamente o mesmo núcleo DSP do arquivo `../univibe_rp2350_dma.cpp`.
- Permitir testar em arquivo completo (WAV/MP3) antes de gravar na placa.
- Validar mudanças DSP com medições objetivas (WAV + CSV), incluindo A/B simples.

## Como a paridade com o Pico funciona

No build da CLI, o script `scripts/extract_dsp.py` extrai o trecho DSP direto de `../univibe_rp2350_dma.cpp` e gera:

- `build/desktop_tools/generated/vibe_core.generated.hpp`

Sempre que o DSP do Pico mudar, a CLI recompila usando esse trecho atualizado.

## Compilar CLI

```powershell
cmake -S desktop_tools -B build/desktop_tools -G Ninja
cmake --build build/desktop_tools -j
```

Executaveis gerados:

- `build/desktop_tools/univibe_cli.exe`
- `build/desktop_tools/dsp_validate.exe`

## Usar CLI de processamento (arquivo único)

```powershell
build/desktop_tools/univibe_cli.exe ^
  --input input.wav ^
  --output output.wav ^
  --mode chorus ^
  --rate 1.2 ^
  --depth 0.6 ^
  --width 0.8 ^
  --feedback 0.45 ^
  --seed 1 ^
  --wav-format pcm16
```

Entrada aceita WAV; para MP3 usa `ffmpeg` se estiver no PATH.
Saida recomendada: `--wav-format pcm16` (mais compatível).

## Harness de validação offline (`dsp_validate`)

Ferramenta para regressão de DSP. Ela:

1. Gera sinais de teste:
   - impulso
   - seno em múltiplos níveis
   - sweep logarítmico
   - tom sintético tipo guitarra
2. Processa offline por preset/voicing (`classic`, `subtle`, `deep`, `vibrato`).
3. Exporta WAV de entrada/saída e CSV com métricas.
4. Mede/loga:
   - resposta em frequência aproximada (`frequency_response.csv`)
   - rastreamento aproximado de notch no tempo (`notch_tracking.csv`)
   - THD por nível de drive (nível de entrada) (`thd_vs_drive.csv`)
   - energia de alta frequência (proxy de aliasing) (`summary.csv`, `thd_vs_drive.csv`)
5. Facilita A/B com baseline via `--compare-to`.

### Exemplo rápido

```powershell
build/desktop_tools/dsp_validate.exe ^
  --out-dir analysis/new ^
  --preset classic ^
  --preset deep
```

### Exemplo A/B (old vs new)

```powershell
build/desktop_tools/dsp_validate.exe ^
  --out-dir analysis/new ^
  --preset classic ^
  --compare-to analysis/old
```

Isso gera, por preset:

- `signals/*_in.wav` e `signals/*_out.wav`
- `metrics/frequency_response.csv`
- `metrics/notch_tracking.csv`
- `metrics/thd_vs_drive.csv`
- `metrics/summary.csv`
- `metrics/summary_vs_baseline.csv` (quando `--compare-to` é usado)

## GUI Python

```powershell
python desktop_tools/gui/univibe_gui.py
```

A GUI chama a CLI e permite ouvir a saída. Se `ffplay` estiver no PATH, ele é usado para reprodução.

## Observações

- Para equivalência com o firmware atual, a validação roda em 44.1 kHz.
- Modo `chorus` e `vibrato` estão suportados.

# Desktop Tools (CLI + GUI)

Esta pasta adiciona um fluxo de teste offline no PC sem mexer no firmware do Pico.

## Objetivo

- Reusar automaticamente o mesmo núcleo DSP do arquivo `../univibe_rp2350_dma.cpp`.
- Permitir testar em arquivo completo (WAV/MP3) antes de gravar na placa.
- Validar mudanças DSP com medições objetivas (WAV + CSV), incluindo A/B simples.

## Como a paridade funciona

As ferramentas incluem diretamente `src/dsp/vibe_core.hpp`, o mesmo core do
firmware, VST e WASM. Nenhum DSP é extraído ou duplicado.

## Compilar CLI

```powershell
cmake -S desktop_tools -B build/desktop_tools -G Ninja
cmake --build build/desktop_tools -j
```

Executaveis gerados:

- `build/desktop_tools/univibe_cli.exe`
- `build/desktop_tools/dsp_validate.exe`
- `build/desktop_tools/vst_param_manifest.exe`

## Usar CLI de processamento (arquivo único)

```powershell
build/desktop_tools/univibe_cli.exe ^
  --input input.wav ^
  --output output.wav ^
  --mode chorus ^
  --rate 1.2 ^
  --depth 0.6 ^
  --stereo-width 0.8 ^
  --feedback 0.45 ^
  --seed 1 ^
  --wav-format pcm16
```

Entrada aceita WAV; para MP3 usa `ffmpeg` se estiver no PATH.
Saida recomendada: `--wav-format pcm16` (mais compatível).

Controles sonoros úteis para calibração pré-VST:

- `--stereo-width <0..1.35>` sobrescreve a largura estéreo calibrada pelo preset.
- `--lamp-lag <0.35..2.5>` multiplica a inércia óptica de ataque/release da lâmpada.
- `--tempo-sync --tempo-bpm <30..300> --tempo-division-beats <0.25..16>` sincroniza o LFO por BPM, usando beats por ciclo.
- `--noise <0..1>` adiciona ruído analógico opcional para voicings lo-fi ou calibração de caráter.

## Harness de validação offline (`dsp_validate`)

Ferramenta para regressão de DSP. Ela:

1. Gera sinais de teste:
   - impulso
   - seno em múltiplos níveis
   - sweep logarítmico
   - tom sintético tipo guitarra
2. Processa offline por preset/voicing (`classic`, `subtle`, `deep`, `vibrato`) ou por aliases de calibracao pre-VST (`classic_univibe`, `shin_ei_dark`, `deja_vibe`, `voodoo_wide`).
3. Exporta WAV de entrada/saída e CSV com métricas.
4. Mede/loga:
   - resposta em frequência aproximada (`frequency_response.csv`)
   - rastreamento aproximado de notch no tempo (`notch_tracking.csv`)
   - THD por nível de drive (nível de entrada) (`thd_vs_drive.csv`)
   - energia de alta frequência (proxy de aliasing) (`summary.csv`, `thd_vs_drive.csv`)
   - score de calibracao por voicing (`calibration_score.csv`, `calibration_summary.csv`)
   - sweep de interacao dry/wet quando `--mix-sweep` esta ativo (`mix_sweep.csv`)
5. Facilita A/B com baseline via `--compare-to`.

### Exemplo rápido

```powershell
build/desktop_tools/dsp_validate.exe ^
  --out-dir analysis/new ^
  --preset classic ^
  --preset deep

build/desktop_tools/dsp_validate.exe ^
  --out-dir analysis/synced ^
  --preset stereo ^
  --quality high ^
  --tempo-sync ^
  --tempo-bpm 96 ^
  --tempo-division-beats 2 ^
  --noise 0.08

build/desktop_tools/dsp_validate.exe ^
  --out-dir analysis/calibration ^
  --calibration-suite ^
  --quality high
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
- `metrics/calibration_score.csv`
- `metrics/mix_sweep.csv` (quando `--mix-sweep` ou `--calibration-suite` é usado)
- `calibration_summary.csv` na pasta raiz de saída
- `metrics/summary_vs_baseline.csv` (quando `--compare-to` é usado)

## Manifesto de parâmetros para VST

O utilitário `vst_param_manifest.exe` exporta a lista estável de parâmetros do core DSP em CSV. Use este arquivo como contrato inicial para IDs de automação, nomes de parâmetros, unidades, faixas e defaults no futuro plugin VST.

```powershell
build/desktop_tools/vst_param_manifest.exe > build/desktop_tools/vst_params.csv
```

Colunas geradas:

- `id`: índice estável do `VibeParamId`.
- `stable_name`: nome persistente para automação/presets.
- `label`: nome legível para UI.
- `unit`: unidade sugerida para host.
- `min`, `max`, `default`: faixa usada pelo core.
- `flags`: dicas para host (`continuous`, `boolean`, `log_scale`, `tempo`, `output`).
## GUI Python

```powershell
python desktop_tools/gui/univibe_gui.py
```

A GUI chama a CLI e permite ouvir a saída. Se `ffplay` estiver no PATH, ele é usado para reprodução.

## Observações

- Para equivalência com o firmware atual, a validação roda em 44.1 kHz.
- Modo `chorus` e `vibrato` estão suportados.

## Baseline da versão 0.9.0

`dsp_validate --preset factory_0` até `--preset factory_11` usa a mesma tabela
musical do plugin, com seed 1 e condicionamento final habilitado. Escolha Quality
explicitamente (`--quality high` para a baseline M0/M1). Os aliases antigos de
calibração continuam disponíveis com seus valores anteriores, distintos do banco
musical do VST. O harness de métricas usa 44.1 kHz; os smoke tests JUCE cobrem
44.1/48/96/192 kHz, mono/stereo e blocos arbitrários.

## M2: análise óptica e comparação controlada

O core usa ReferenceOptical por padrão. `dsp_validate --optical legacy` seleciona
somente a óptica M0/M1, mantendo o mesmo DSP de áudio. Esse seletor é diferente
do antigo `--engine legacy` da CLI. IDs públicos e modos Quality permanecem iguais.

```powershell
build/desktop_tools/optical_analyze.exe --out-dir build/m2/optics
python desktop_tools/scripts/validate_optical.py build/m2/optics/summary.csv
build/desktop_tools/optical_analyze.exe --cpu-only --out-dir build/m2/cpu

$factoryArgs = 0..11 | ForEach-Object { '--preset'; "factory_$_" }
build/desktop_tools/dsp_validate.exe --out-dir build/m2/legacy --quality high --optical legacy @factoryArgs
build/desktop_tools/dsp_validate.exe --out-dir build/m2/reference --quality high --optical reference --factory-levels baseline @factoryArgs
python desktop_tools/scripts/compare_optical.py build/m2/legacy build/m2/reference build/m2/comparison
```

`optical_analyze` exporta 0.2/0.5/1/2/4/7 Hz × Depth 0.15/0.35/0.60/0.85/1.00
para ambos os modelos: fase, drive, brilho e quatro resistências. São quatro
ciclos completos após estabilização, observados a cada 32 amostras. O modelo
processa todas as amostras. `--sample-rate` permite repetir em outra taxa.
`*_averaged.csv` contém médias em 256 bins de fase; `summary.csv` contém extremos,
médias, média geométrica, razão de modulação, atraso e estabilidade por ciclo.
Cell=-1 representa a lâmpada; 0..3 representam os LDRs. Tempos de subida/descida
10–90% são medidas da trajetória periódica, diferentes das constantes térmicas.
`configuration.txt` registra controles, taxa, seed e tamanho dos componentes.

`cpu.csv` mede o passo óptico e o core completo nas três Qualities. Tempos máximos
incluem escalonamento do sistema desktop e não garantem deadlines no RP2350.

Legacy com factory presets mantém os ganhos originais. Reference com
`--factory-levels baseline` usa esses mesmos ganhos para isolar a alteração
óptica. Reference sem essa opção usa os cinco ajustes M2 do banco atual.
`compare_optical.py` preserva eventos de notch separadamente quando suas
quantidades mudam. `compare_regression.py` continua exigindo igualdade estrutural
para reproduzir a baseline M0/M1.

Com matplotlib instalado, gere um gráfico estático:

```powershell
python desktop_tools/scripts/plot_optical.py build/m2/optics build/m2/trajectories.png
```

Arquitetura, equações, resultados e limitações:
[docs/m2-optical-reference.md](../docs/m2-optical-reference.md).

## M2.1 optical calibration and topology

`optical_analyze --out-dir DIR` renders the unchanged six-speed/five-intensity
matrix with one lamp, exports all four cells and lamp, and writes `calibration.csv`.
Aggregate means give each Speed × Intensity trajectory equal weight, rather than
letting slow trajectories dominate by sample count. Run
`python desktop_tools/scripts/validate_optical.py DIR/summary.csv` for the separate
optical fidelity checks. Paper extrema constrain cell capability; they are not
required at every trajectory. No exact measured fitting is claimed.

`dsp_validate --optical reference --topology reference` selects one physical lamp;
`--topology studio` (default) selects production phase-offset stereo optics.
These are desktop/internal selections, not public VST parameters. Reference
optics disable creative drift/phase offsets while preserving the existing audio
processing and output conditioner. `--factory-levels baseline` freezes M0/M1
gains for comparison with M2 (current factory gains are also reverted to M0/M1).

`notch_trajectory OUTPUT.csv` exports frozen linear notch minima and stage corners
for all factory optical settings: seed 1, High, 44.1 kHz, drift/width zero. The
probe uses the actual stage target coefficients plus equal dry/wet summation;
it excludes feedback, distortion, Studio wet compensation and conditioning.
It also probes Classic Vibrato with equal dry/wet, so its minima describe circuit
phase cancellation potential, not vibrato-output notches. Branch IDs order the
currently visible minima by frequency; they are not persistent notch identities.
For M2 comparison compile this source with `-DM2_BASELINE` and headers extracted
from `252063c`. Full production audio metrics remain in `dsp_validate`.

Detailed constraints, tolerances, results and reproduction commands:
[docs/m2-1-optical-calibration.md](../docs/m2-1-optical-calibration.md).

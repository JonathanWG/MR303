# Handoff — estado em 10/09/2026 (repo publicado; ResampleRecorder recuperado)

Documento de continuidade. Descreve exatamente onde o projeto parou, o que é
verdade verificada e o que é código escrito mas nunca executado.

---

## Contexto em um parágrafo

Plugin de áudio (VST3/AU) inspirado no Roland/Boss SP-303, em C++20 com JUCE.
Decisão tomada: **Caminho A** — construir um sampler funcional e bom, *sem*
depender de acesso ao hardware físico. Isso significa que o produto **não é
vendido como emulação** até a Fase 0 (medição) rodar. Pesquisa de base em
`docs/research/SP-303_Estudo_Completo.md`, plano de engenharia em
`docs/research/SP-303_Plugin_Plano_Engenharia.md`.

---

## O fato mais importante

**O portão foi atravessado: o projeto compila e os testes rodam.**

Toolchain instalado em 26/08/2026: CMake 4.4.2 + MSVC 19.44 (VS 2022 Build
Tools 14.44.35207), Windows SDK 10.0.26100.

| O quê | Resultado |
|---|---|
| `core` + testes (Release, MSVC) | compila, **0 erros** |
| `ctest` | **91/91 passam** (44 pré-existentes + 47 novos) |
| `plugin` (JUCE 8.0.4, target Standalone) | compila, 0 erros — depois de 1 correção, abaixo |
| Avisos | só `C4324` (o `alignas(64)` proposital em `SpscQueue`) e um `C4458` cosmético em `PluginProcessor.cpp:136` |

Correção necessária para o plugin compilar: `plugin/src/PluginProcessor.h`
incluía apenas `juce_audio_processors.h`, que **não** declara
`AudioFormatManager`, `AudioFormatReader` nem `FlacAudioFormat`. Faltava
`#include <juce_audio_formats/juce_audio_formats.h>`. Era o único erro real de
compilação em todo o código escrito às cegas.

Nota de build: `cmake --build ... --parallel` no target do plugin estoura a
memória do MSVC dentro do Harfbuzz do JUCE (`error C1060`). Compile o plugin
com `/m:1`. O core em paralelo vai bem.

O que **continua** não verificado: nada foi comparado com hardware. A Fase 0
não rodou. "Compila e passa nos testes" não é "soa como um SP-303".

---

## Arquitetura em 30 segundos

```
core/     Layer 0 — C++ puro, ZERO dependência de JUCE. O instrumento de fato.
plugin/   Layers 1–4 — wrapper JUCE. Quase sem lógica: só traduz host <-> core.
tools/    Python — medição, identificação, null-test, gerador de skin.
docs/     Arquitetura, fatos de hardware, protocolo Fase 0, licenciamento.
```

`core/` não depende de JUCE de propósito: testa headless, roda no harness Python
sem DAW, e trocar de framework não toca uma linha de DSP.

**A regra única:** tudo alcançável de `Device::processBlock()` roda em thread
real-time com prazo de ~2,7 ms. Nunca aloca, nunca trava lock, nunca toca disco.
Marcado com `SP303_RT`. Sob clang com `SP303_RT_SANITIZE=ON` isso vira contrato
verificado em runtime, e o CI roda esse build.

Canais com a thread de áudio, e só estes:

| Dado | Mecanismo |
|---|---|
| Comandos discretos | `rt::SpscQueue<Command>` |
| Parâmetros contínuos (CTRL 1/2/3) | `std::atomic<float>` + `ParameterSmoother` |
| Áudio dos samples | `SampleSlot` — publicação de ponteiro cru atômico + hazard pointers |
| Telemetria p/ UI | `rt::SpscQueue<Telemetry>`, lossy de propósito |

---

## Funciona (escrito + coberto por teste)

- 8 vozes polifônicas, todos os modos de disparo
  (Trigger/Gate × Loop/OneShot × Normal/Reverse × Hold)
- START / END / LEVEL por pad, 4 bancos de 8
- Filas lock-free, testadas sob concorrência real (100k itens)
- Interpolação (drop-sample / linear / cúbica), biquad, saturação,
  quantização, decimação, FIR, oversampler
- Os 5 efeitos de acesso direto: Filter+Drive, Pitch, Delay, Vinyl Sim, Isolator
- Matemática de swing/quantize
- Reclamação de memória sem lock: ponteiro cru atômico + hazard pointers
- Captura de resample, ligada ao painel (RESAMPLE → pad → REC → REC)
- **91 testes** em 10 arquivos

## Escrito, sem cobertura de regressão

- **Persistência de estado** — `get/setStateInformation`. Pad settings + áudio
  embutido como FLAC 24-bit, serializado via `ValueTree::writeToStream` (binário,
  não XML — XML faria base64 de cada blob). Tem fallback para ler o formato XML
  antigo.
- **Sistema de skin** — `plugin/src/Skin.{h,cpp}` + `PanelComponent.{h,cpp}`.
  Painel = imagem de fundo + mapa de hitboxes, ambos gerados de UMA definição de
  layout em `tools/skin/build_skin.py` para não divergirem. Overlays desenhados
  por cima para o que a arte não sabe (pad aceso, posição do knob, o que os CTRL
  fazem agora).

## Não existe

- 21 dos 26 efeitos (os 5 de acesso direto existem; faltam os MFX)
- Sequenciador de patterns — os tipos existem, `processBlock` é no-op honesto
- MARK / DEL / ST-END / TIME-BPM / REMAIN e os toggles de playback: desenhados
  e com hitbox, mas inertes. Continuam caindo no `else` de
  `PanelComponent::performAction` e só exibem "not implemented yet".
  RESAMPLE / REC / CANCEL saíram dessa lista — ver "Feito em 26/08/2026"
- Fase 0 (medição de hardware) — 0%, bloqueada em conseguir um SP-303 físico

---

## Armadilhas conhecidas

1. **`ResampleRecorder.cpp` acabou de ser adicionado ao `core/CMakeLists.txt`.**
   Estava faltando e teria dado erro de link. Se aparecerem símbolos
   indefinidos, procure outros `.cpp` fora da lista.
2. ~~**`SampleSlot` usa `std::atomic<std::shared_ptr<T>>`.**~~ **RESOLVIDO em
   05/09/2026.** A medição confirmou o pior caso — `is_lock_free()` retornou
   `NO` no MSVC 19.44 / x64, ou seja, a thread de áudio travava um lock em toda
   `SampleSlot::load()`. Trocado por publicação de **ponteiro cru atômico**
   (lock-free em todo alvo) mais **hazard pointers** e lista de aposentadoria.
   Ver `docs/ARCHITECTURE.md` e `core/tests/test_reclamation.cpp`.
3. ~~**Destrutor de `SampleBuffer` na thread real-time.**~~ **RESOLVIDO junto
   com o item 2.** `Voice` guarda ponteiro cru, então `stop()` não solta mais
   referência nenhuma; quem libera é `Device::collectRetiredSamples()`, na
   message thread, chamado pelo timer de 20 Hz do processor.
4. **`Oversampler::processSample` é template de propósito.** Não converter para
   `std::function`: o lambda do Filter+Drive estoura o small-buffer e alocaria no
   loop interno da thread de áudio.
5. **`plugin/CMakeLists.txt` copia as skins para junto do binário** em
   POST_BUILD. Antes de release isso tem que virar `juce_add_binary_data` — um
   plugin distribuído não pode depender de arquivos soltos ao lado do `.vst3`.

---

## Pendências jurídicas que travam release (não são papelada pra depois)

Detalhe em `docs/LICENSING.md`.

- **Nome do produto** — `PRODUCT_NAME "SP303"` no CMake é placeholder e não pode
  ir para produção. Marcas "Roland", "Boss", "SP-303" são da Roland.
- **Licença JUCE** — GPL ou comercial, indefinido. Determina se o plugin pode ser
  closed-source, então bloqueia a Fase 2.
- **Firmware** — proibido dumpar, desmontar ou portar. Só engenharia reversa
  comportamental de caixa-preta.
- **Trade dress** — a skin default é design original de propósito. Se trocar por
  foto de hardware, é foto da própria unidade, e a questão de distribuição
  continua aberta.

---

## Feito em 26/08/2026

1. **Toolchain e primeiro build verde.** Ver "O fato mais importante".
   ```
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release --parallel
   ctest --test-dir build -C Release --output-on-failure     # 57/57
   ```
   Plugin (JUCE é baixado no primeiro configure — leia `docs/LICENSING.md`):
   ```
   cmake -B build-plugin -DSP303_BUILD_PLUGIN=ON -DCMAKE_BUILD_TYPE=Release
   cmake --build build-plugin --config Release --target sp303_plugin_Standalone -- /m:1
   ```

2. **RESAMPLE / REC / CANCEL ligados** em `PanelComponent::performAction`.
   Fluxo escolhido, na ordem do hardware:
   **RESAMPLE → pad (escolhe o destino) → REC (começa) → REC (para)**.
   Enquanto espera o destino os pads viram seletor e não tocam, e o release do
   pad consumido não vira NoteOff. RESAMPLE durante a captura também para.
   CANCEL aborta; sem nada para abortar, vira pânico (AllNotesOff).
   O display de 3 dígitos mostra `rSP` ao armar e a % de buffer ao gravar —
   estourar o buffer é a única coisa que encerra a take sem o usuário pedir.

3. **13 testes para `ResampleRecorder`** — arm/start/write/stop, truncar e parar
   sozinho no estouro, `start()` só saindo de `Armed`, `stop()` só saindo de
   `Recording`, slot fora de faixa, argumentos degenerados, cancel, e
   re-`prepare()` limpando a take anterior.

4. **Os 4 efeitos diretos restantes** — Pitch, Delay, Vinyl Sim, Isolator, mais
   `dsp::DelayLine` (linha de atraso fracionária compartilhada por três deles).
   Todos registrados em `EffectRack`. Verificados por harness descartável (não
   versionado): saída finita e limitada em 21 combinações de controles,
   incluindo realimentação máxima e entrada em silêncio. O Isolator mede
   ±0,02 dB de 50 Hz a 16 kHz com os três botões ao centro.

---

## Feito em 05/09/2026

5. **`SampleSlot` reescrito — a thread de áudio não trava mais lock.**
   `std::atomic<shared_ptr>` saiu; entrou publicação de ponteiro cru atômico
   (lock-free), `rt::HazardPointers<kMaxVoices>` publicado ao fim de cada bloco
   por `VoiceManager::render()`, e uma lista de aposentadoria em `Device`
   drenada por `collectRetiredSamples()` na message thread. Isso resolve as
   armadilhas 2 e 3 de uma vez: nada aloca, trava ou libera na thread RT.
   A regra de liberação tem duas condições, ambas necessárias — o bloco em que
   o buffer foi aposentado precisa ter terminado, **e** nenhum hazard pode
   nomeá-lo. 12 testes novos em `test_reclamation.cpp`.

6. **Testes versionados para os 4 efeitos** — 26 casos em `test_effects.cpp`,
   incluindo `dsp::DelayLine`. As propriedades que valem: bypass exato em
   LEVEL/MIX 0, o crossfade de Hann do Pitch sendo neutro em ganho em todos os
   deslocamentos, ruído determinístico do Vinyl Sim, e o Isolator plano em
   magnitude de 50 Hz a 16 kHz.

---

## Feito em 10/09/2026

7. **Repositório publicado.** `git@github-pessoal:JonathanWG/MR303.git`, branch
   `main`, nome do projeto **ManjericãoRoxo303**. Os docs de base saíram da
   pasta-mãe e agora vivem em `docs/research/` (links do README/HANDOFF
   atualizados). `build/`, `build-plugin/` e binários ficam fora via `.gitignore`.

8. **`ResampleRecorder.h/.cpp` recuperados.** A sessão de 05/09 às 19:27 rodou
   `rm -f` nos dois arquivos "para reescrever do zero", escreveu os efeitos novos
   em seguida e terminou **sem** reescrever o recorder. A árvore ficou sem
   configurar no CMake por 5 dias (`Cannot find source file: src/ResampleRecorder.cpp`).
   Reconstrução feita a partir do transcrito daquela sessão: a versão pretendida
   (com `enum class Source { Output, Input }`, `arm(slot, source)`,
   `isRecordingFrom()`, `isListeningToInput()`) mais a correção de ordem em
   `Device::processBlock` (`drainCommands()` antes da leitura do input).
   **Verificado hoje:** core compila em Release (MSVC 19.44) e `ctest` passa
   **91/91** com binário novo. Os 13 testes de `test_resample_recorder.cpp`
   continuam passando contra a API nova.

9. **`test_input_sampling.cpp` nunca existiu.** Estava listado em
   `core/tests/CMakeLists.txt` mas nenhuma sessão chegou a escrevê-lo; removido
   da lista. O sampling da entrada de áudio está **meio-feito**: `Device` já tem
   `armInputSampling()` e o tap do input, o recorder distingue as fontes, mas
   não há teste de regressão e o painel (`PanelComponent`) não dispara a fonte
   Input. Continua sendo o passo 1 abaixo.

10. **Os 5 MFX escritos em 05/09 compilam, e só isso.** `Reverb`, `TapeEcho`,
    `Chorus`, `Flanger`, `Phaser` e `dsp::Lfo` foram escritos no fim daquela
    sessão, estão registrados em `EffectRack` e entraram no build de hoje sem
    erro. **Zero testes, nunca ouvidos, não documentados em
    `docs/ARCHITECTURE.md`.** Tratar como `UNVERIFIED` até ganharem casos em
    `test_effects.cpp` (as propriedades que valem lá: bypass exato em MIX 0,
    saída finita e limitada com realimentação máxima, silêncio em silêncio).

11. **Plugin não foi recompilado hoje.** O último build verde do target
    Standalone é o de 26/08. O `PanelComponent` só usa
    `ResampleRecorder::State`, que manteve os mesmos valores, então deve
    compilar — mas "deve" não é "compilou".

---

## Próximos passos, em ordem

1. **Sampling da entrada de áudio.** REC hoje só existe dentro do fluxo de
   resample; gravar do input do canal ainda não existe.

2. **Rodar o build do RealtimeSanitizer.** Agora que o design está correto no
   papel, `SP303_RT_SANITIZE=ON` sob clang é o que transforma isso em contrato
   verificado. Nunca foi executado — precisa de clang 20+, que não está na
   máquina.

3. Sequenciador, GUI final, os 21 MFX.

4. **Fase 0** — continua 0%, continua bloqueada em conseguir um SP-303 físico.
   Nada acima aproxima o produto de "emulação"; só o torna um sampler melhor.

<!-- passos originais, mantidos para referência histórica:

1. **Instalar toolchain e conseguir o primeiro build verde.**
   ```
   winget install Kitware.CMake Microsoft.VisualStudio.2022.BuildTools
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --parallel
   ctest --test-dir build --output-on-failure
   ```
   Espere erros de compilação — nada disso passou por um compilador ainda.

2. **Ligar RESAMPLE e REC** em `PanelComponent::performAction`. O backend já
   existe (`processor_.armResample/startResample/stopResample`); falta o
   despacho das ações `"resample"` e `"rec"`, e uma noção de qual pad é o
   destino.

3. **Testar `ResampleRecorder`** — arm/start/write/stop, estouro de buffer
   (deve truncar e parar sozinho), e que `start()` só sai de `Armed`.

4. **Os 4 efeitos diretos restantes** — Pitch, Delay, Vinyl Sim, Isolator.
   `FilterDrive` é o modelo de referência: mapeamento exponencial onde a
   percepção é logarítmica, não-linearidade dentro de bloco oversampled,
   `IEffect` como contrato.

5. Só então: sequenciador, GUI final, os 21 MFX.

-->

---

## Enquadramento honesto

Este repositório representa cerca de **4–5 semanas-equivalente** de um dev
sênior, de um total estimado de 33–49 semanas para o produto completo. O que
está aqui é a fundação certa — modelo de threads, separação de camadas, infra de
teste, sistema de skin — que é justamente a parte cara de retrofitar depois.

Agora compila e passa nos testes, o que torna as estimativas acima confiáveis
pela primeira vez — e a rachadura que o primeiro build verde revelou
(`SampleSlot` travando lock na thread de áudio) foi consertada e coberta por
teste. Mas "passa nos testes" não é "soa certo": a Fase 0 não rodou, e a
segurança real-time está argumentada e testada, não verificada por sanitizer.

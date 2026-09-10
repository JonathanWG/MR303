# Plugin "SP-303-like" — Plano de Engenharia de Software e Arquitetura

**Objetivo:** um plugin de áudio (VST3/AU/CLAP) que replique o comportamento, o fluxo de trabalho e o caráter sonoro do Roland/Boss SP-303, rodando em Ableton Live e outras DAWs — com interface fiel, carregamento de samples, sequenciador, resample e usabilidade "hardware-like".

Documento complementar ao [SP-303_Estudo_Completo.md](SP-303_Estudo_Completo.md).

---

## Sumário

1. [Veredito e decisões de topo](#1-veredito-e-decisões-de-topo)
2. [Por que não Python (e onde Python é essencial)](#2-por-que-não-python-e-onde-python-é-essencial)
3. [Stack recomendada](#3-stack-recomendada)
4. [Espectro de fidelidade: escolher o alvo antes de codar](#4-espectro-de-fidelidade-escolher-o-alvo-antes-de-codar)
5. [Fase 0 — Engenharia reversa por medição](#5-fase-0--engenharia-reversa-por-medição)
6. [Arquitetura de software](#6-arquitetura-de-software)
7. [Modelo de domínio e estado](#7-modelo-de-domínio-e-estado)
8. [O que precisa ser modelado no DSP](#8-o-que-precisa-ser-modelado-no-dsp)
9. [Integração com a DAW](#9-integração-com-a-daw)
10. [Interface e usabilidade](#10-interface-e-usabilidade)
11. [Testes, QA e CI/CD](#11-testes-qa-e-cicd)
12. [Riscos: técnicos e jurídicos](#12-riscos-técnicos-e-jurídicos)
13. [Roadmap, esforço e complexidade](#13-roadmap-esforço-e-complexidade)
14. [Definição de MVP](#14-definição-de-mvp)

---

## 1. Veredito e decisões de topo

| Decisão | Escolha | Justificativa em uma linha |
|---|---|---|
| Linguagem do plugin | **C++17/20** | Único caminho maduro para áudio em tempo real sem GC e sem pausas |
| Framework | **JUCE** (alternativa: Rust + `nih-plug`) | Um codebase → VST3 + AU + AAX + standalone; ecossistema e documentação |
| Papel do Python | **Ferramental offline** (medição, ajuste de coeficientes, null-test, CI) | Onde ele é imbatível e onde a latência não importa |
| Alvo de fidelidade | **Nível 2+ (comportamental, verificado por null-test)** | O núcleo do SP-303 é digital → precisão alta é factível sem modelagem de circuito |
| Formatos | VST3 + AU (fase 1) · CLAP + AAX (fase 2) | VST3/AU cobrem Ableton, Logic, Reaper, Bitwig, FL |
| Esforço total | **~8–12 meses-pessoa** para produto completo · **~3 meses** para MVP | Ver §13 |
| Maior risco técnico | Os **26 efeitos** — é a fatia longa do cronograma | Ver §13 |
| Maior risco não-técnico | **Trade dress / marca Roland** | Ver §12 |

---

## 2. Por que não Python (e onde Python é essencial)

### 2.1 A restrição dura: o audio callback

Um plugin roda dentro de um *callback* de áudio em thread de prioridade real-time. Com buffer de 128 amostras a 48 kHz, o prazo de entrega é **~2,67 ms por bloco**. Estourar esse prazo uma única vez = clique audível (*xrun*).

Dentro desse callback é **proibido**:

- alocar/desalocar memória (`malloc`, `new`, `std::vector::push_back`)
- adquirir locks (mutex) que a UI também possa segurar
- I/O de disco ou rede
- qualquer coisa cujo tempo de execução não seja limitado por cima

Python viola isso estruturalmente: **GIL** (contenção com qualquer outra thread), **garbage collector** com pausas não determinísticas, e alocação implícita em praticamente toda operação. Não existe hoje um caminho de produção para DSP em tempo real em Python — bibliotecas como `pedalboard` da Spotify existem para *hospedar* e processar **offline**, não para escrever o plugin.

### 2.2 Onde Python é a ferramenta certa

Python carrega uma parte crítica e frequentemente subestimada do projeto — **toda a Fase 0** e a infraestrutura de qualidade:

- Automação da captura de medições do hardware (varreduras, impulsos, matriz de parâmetros)
- Análise espectral e identificação de sistema (`numpy`, `scipy.signal`)
- Ajuste de coeficientes de filtro e de curvas de saturação (`scipy.optimize`)
- Geração dos *golden files* de regressão
- **Harness de null-test** que roda no CI e falha o build se o resíduo contra o hardware piorar
- Conversão e inspeção de dumps de cartão SmartMedia

Ou seja: Python **não** é o plugin, mas é o que separa "mais um plugin lo-fi" de "emulação verificada".

---

## 3. Stack recomendada

### 3.1 Framework do plugin

| Opção | Prós | Contras | Recomendação |
|---|---|---|---|
| **JUCE** (C++) | Maduro; VST3/AU/AAX/standalone de um codebase; GUI, formatos de áudio, MIDI, gerenciamento de estado prontos; maior base de conhecimento | Licenciamento GPL ou comercial (verificar tier atual antes de começar); GUI própria, não nativa | **Primária** |
| **nih-plug** (Rust) | Segurança de memória sem GC; CLAP nativo + VST3; build moderno | Ecossistema menor; GUI menos madura; menos exemplos de emulação complexa | Alternativa forte se o time já for Rust |
| **iPlug2** (C++) | Licença permissiva; suporta CLAP/WAM | Comunidade menor, menos documentação | Se a licença JUCE for bloqueante |
| **Faust** | Excelente para blocos de DSP puro | Fraco para máquina de estados, sequenciador, GUI complexa | Usar **embutido** para prototipar filtros, não como framework |

**Recomendação:** JUCE como framework, com o núcleo de DSP escrito como **biblioteca C++ pura, sem dependência de JUCE** — isso permite testar o motor headless, rodar no CI e, se necessário, trocar de framework depois.

### 3.2 Ferramental

- **Build:** CMake (JUCE 7+ tem suporte nativo, funciona bem no CI)
- **Testes:** Catch2 ou GoogleTest para o núcleo de DSP
- **Validação de plugin:** `pluginval` (Tracktion) — obrigatório, pega bugs de compatibilidade de host
- **CI:** GitHub Actions com runners macOS (universal binary arm64 + x86_64) e Windows x64
- **Análise:** Python 3.12 + numpy/scipy/matplotlib/soundfile

---

## 4. Espectro de fidelidade: escolher o alvo antes de codar

Esta é a decisão que mais afeta custo. Três níveis:

| Nível | O que é | Esforço | Resultado sob escrutínio |
|---|---|---|---|
| **L1 — "Vibe"** | Bitcrusher + downsampler + filtro passa-baixa + ruído de vinil | ~2 semanas | Soa "lo-fi", mas não passa em comparação A/B com o hardware |
| **L2 — Comportamental** | Algoritmos corretos (interpolação, decimação, quantização, cada efeito), fluxo de trabalho idêntico, verificado por null-test | **Alvo deste plano** | Indistinguível na maior parte do material; resíduo típico −35 a −55 dB |
| **L3 — Bit-exato no núcleo digital** | Reimplementação exata dos algoritmos de DSP; só o estágio analógico de I/O é modelado | +30–50% sobre L2, com retorno decrescente | Null perfeito no caminho digital |

**Insight estrutural:** diferente de um sintetizador analógico, **o SP-303 é essencialmente digital** — conversão, decimação, filtro, efeitos e sequenciador são todos DSP. Isso significa que L3 é *teoricamente alcançável* no núcleo, e que L2 é bem mais barato de atingir do que seria emular um instrumento analógico. As partes analógicas são apenas: pré-amp de entrada, estágio de saída/filtro de reconstrução e o circuito de fone.

O plano abaixo mira **L2 com portas L3 abertas** nos blocos que mais importam (interpolação de playback, decimação lo-fi, Filter+Drive).

---

## 5. Fase 0 — Engenharia reversa por medição

**Este é o passo que a maioria dos projetos pula, e é por isso que a maioria soa genérica.**

A documentação pública sobre o SP-303 é ambígua em pontos decisivos. Exemplos concretos de perguntas que **nenhuma fonte responde de forma confiável** e que precisam ser medidas:

- A profundidade real de bits do caminho interno (fontes públicas citam 20 bits nos conversores, mas isso não determina a resolução do caminho de dados)
- Qual **interpolação** é usada ao transpor/variar a velocidade (drop-sample? linear? cúbica?) — é o maior determinante do caráter em pitch shift
- Se o "time-stretch" de 50–130% preserva pitch (algoritmo granular, com artefatos próprios) ou é varispeed puro (afeta o pitch, como vinil) — as fontes se contradizem
- A curva exata do **Drive** e a topologia do filtro (número de polos, tipo, comportamento da ressonância)
- Se há **dither** ou truncamento puro na quantização
- Comportamento do filtro anti-aliasing nos modos Long/Lo-Fi — quanto aliasing é deixado passar de propósito

### 5.1 Protocolo de medição

**Hardware necessário:** uma unidade SP-303 + interface de áudio de qualidade capturando a 96 kHz/24 bits.

1. **Caminho seco (bypass):** injetar sinais conhecidos (varredura logarítmica, impulso, ruído branco, senoides puras, degraus DC) e gravar a saída em cada modo (Standard/Long/Lo-Fi, mono/estéreo). Isola resposta em frequência, ruído de fundo, latência e o estágio de saída.
2. **Matriz de transposição:** um mesmo sample tocado em cada valor de pitch/velocidade. A distribuição de produtos de aliasing identifica a interpolação usada.
3. **Matriz por efeito:** para cada um dos 26 efeitos, varrer CTRL1/CTRL2/CTRL3 em uma grade (ex.: 8×8×8) com sinal de teste padronizado. Automatizável em parte via MIDI.
4. **Caracterização não-linear:** senoides em amplitudes crescentes através do Filter+Drive → medir THD e a estrutura harmônica para ajustar a curva de saturação.
5. **Comportamento de sequenciador:** gravar performances com grid conhecido e medir onde cada evento realmente cai → extrai a matemática exata do quantize e do swing.

### 5.2 Entregável da Fase 0

Um **dataset versionado** (`.wav` + metadados JSON) e um relatório com os parâmetros identificados. Esse dataset vira a base do harness de null-test que roda no CI para sempre.

> **Ponto jurídico crítico:** essa é engenharia reversa **comportamental, de caixa-preta** — medir entradas e saídas. **Não** dumpar, desmontar ou portar o firmware da Roland. Ver §12.

---

## 6. Arquitetura de software

### 6.1 Camadas

```
┌──────────────────────────────────────────────────────────────┐
│  CAMADA 4 — WRAPPERS DE FORMATO                              │
│  VST3 · AU · CLAP · AAX · Standalone        (JUCE)           │
├──────────────────────────────────────────────────────────────┤
│  CAMADA 3 — GUI / EDITOR                    (thread da UI)   │
│  Painel · pads · knobs · display 7-seg · browser de samples  │
├──────────────────────────────────────────────────────────────┤
│  CAMADA 2 — CONTROLADOR / ESTADO                             │
│  Máquina de estados do aparelho · params · presets · undo    │
├──────────────────────────────────────────────────────────────┤
│  CAMADA 1 — MOTOR (tempo real)         ★ sem alocação ★      │
│  Voice manager (8 vozes) · sequenciador · efeitos · resample │
├──────────────────────────────────────────────────────────────┤
│  CAMADA 0 — NÚCLEO DE DSP        (C++ puro, sem JUCE)        │
│  Interpolação · decimação · quantização · filtros · 26 FX    │
└──────────────────────────────────────────────────────────────┘

  Serviços laterais (thread de background):
  SampleLoader · disk streaming · render offline · leitor SmartMedia
```

A **Camada 0 não depende de JUCE nem de nada de plugin** — é uma biblioteca testável isoladamente, compilável em qualquer lugar, e é onde vivem os testes de regressão e o null-test.

### 6.2 Modelo de threads — o coração da engenharia

```
   THREAD DE ÁUDIO                THREAD DE MENSAGEM/UI        THREAD DE BACKGROUND
   (prioridade real-time)         (~60 fps)                    (worker pool)
   ──────────────────────         ─────────────────────        ────────────────────
   processBlock()                 repaint / eventos            carregar WAV do disco
   • lê params atômicos           • lê snapshot de estado      • decodificar
   • dispara vozes                • escreve comandos           • converter taxa
   • roda efeitos                 • desenha meters             • render de resample
   • grava buffer de resample                                    offline
        │                               │                            │
        │  ◄── FIFO lock-free (SPSC) ───┤                            │
        │      comandos UI → áudio      │                            │
        │                               │                            │
        ├──── FIFO lock-free ──────────►│                            │
        │      telemetria áudio → UI    │                            │
        │      (posição, níveis, LEDs)  │                            │
        │                                                            │
        └──── troca atômica de ponteiro ◄────────────────────────────┘
              (snapshot imutável do banco de samples)
```

**Padrões obrigatórios:**

- **Parâmetros contínuos:** `std::atomic<float>` + suavização por rampa no áudio (evita zipper noise nos knobs CTRL)
- **Comandos discretos** (trocar modo, carregar pad, iniciar rec): fila SPSC lock-free UI→áudio
- **Dados de sample:** snapshot **imutável**; a thread de background monta o novo banco completo e faz *swap* atômico do ponteiro; o antigo é aposentado depois de N blocos (padrão RCU). O áudio nunca aloca nem espera.
- **Telemetria áudio→UI:** fila lock-free com *drop* permitido (perder um frame de LED é irrelevante)
- **Buffer de resample:** pré-alocado no tamanho máximo antes de armar o REC. Nada de crescer buffer em tempo real.

### 6.3 Estrutura do repositório

```
sp303-plugin/
├── core/                    # Camada 0 — C++ puro, zero deps
│   ├── dsp/                 # interpolação, decimação, quantização, filtros
│   ├── fx/                  # os 26 efeitos, um arquivo por algoritmo
│   ├── voice/               # motor de voz, envelopes, modos de disparo
│   ├── sequencer/           # patterns, quantize, swing, transporte
│   └── tests/               # Catch2 + golden files
├── plugin/                  # Camadas 1–4 — JUCE
│   ├── processor/
│   ├── editor/
│   └── state/
├── tools/                   # Python
│   ├── measure/             # automação de captura do hardware
│   ├── identify/            # ajuste de coeficientes, análise
│   ├── nulltest/            # harness de regressão vs. hardware
│   └── smartmedia/          # leitor de dump de cartão
├── resources/               # assets de GUI, samples de fábrica
└── .github/workflows/       # CI: build, test, null-test, assinatura
```

---

## 7. Modelo de domínio e estado

Modelar o **aparelho**, não o plugin. Isso é o que garante que a usabilidade seja fiel.

```cpp
struct Device {
    Bank            banks[4];          // A, B, C, D
    int             activeBank;
    SampleMemory    memory;            // com limites autênticos opcionais
    PatternStore    patterns;          // 32 patterns, 1–99 compassos
    EffectSlot      currentEffect;     // UM por vez (regra do hardware)
    TransportState  transport;
    RecordState     recorder;          // sampling | pattern rec | resample
    Display         display;           // 3 dígitos, 7 segmentos
};

struct Pad {
    SampleRef  sample;
    uint32_t   startFrame, endFrame;   // START / END
    float      level;                  // LEVEL
    TriggerMode trigger;               // Trigger | Gate
    LoopMode    loop;                  // Loop | OneShot
    bool        reverse;
    float       speedRatio;            // time-stretch 50–130%
    float       bpm;                   // derivado do comprimento
};
```

### 7.1 Decisão de design: "Authentic Mode" vs. "Modern Mode"

Um toggle global que resolve toda a tensão entre fidelidade e usabilidade moderna:

| Aspecto | Authentic | Modern |
|---|---|---|
| Limite de memória | 31 s / 63 s / 3'10" | Ilimitado |
| Slots | 32 (4 bancos × 8) | Bancos ilimitados |
| Efeitos simultâneos | 1 (força o uso de resample) | Cadeia livre |
| Pads | Sem velocity | Responde a velocity MIDI |
| Edição de START/END | Só de ouvido, sem waveform | Waveform visível + zoom |
| Quantize | Destrutivo na gravação | Não-destrutivo, com undo |
| Resample | Só em tempo real, no mesmo banco | Também offline, entre bancos |

Isso não é "modo fácil" — é reconhecer que **as restrições do hardware são uma feature** para quem quer o fluxo original, e um atrito gratuito para quem não quer.

---

## 8. O que precisa ser modelado no DSP

Em ordem de impacto sobre o caráter sonoro:

### 8.1 Interpolação de reprodução — **o maior determinante**

Quando um sample é tocado fora da taxa original (transposição, time-stretch), o algoritmo de interpolação define os artefatos. Drop-sample (zero-order hold) gera espelhamento agressivo; linear gera atenuação de agudos característica; cúbico/sinc é "limpo demais" e mata o caráter.

**Implementar todos, escolher por medição.** É barato implementar e é onde está a assinatura sonora.

### 8.2 Decimação e requantização (modos Long / Lo-Fi)

- Se o filtro anti-aliasing do hardware é agressivo → som abafado limpo
- Se é fraco/inexistente → aliasing deliberado, som "metálico" característico

Precisa medir para decidir. Modelar o filtro identificado, não um passa-baixa genérico.

### 8.3 Quantização de amplitude

Truncamento vs. arredondamento vs. dither muda o piso de ruído e o comportamento em fade-outs. Detalhe pequeno, audível em sons quietos.

### 8.4 Filter + Drive

Filtro ressonante + saturação não-linear. Duas armadilhas:

- A saturação **precisa de oversampling** (4× ou 8×) para não gerar aliasing que o hardware não tem
- Se houver auto-oscilação ou comportamento instável em ressonância alta, isso é parte do som — replicar, não "consertar"

### 8.5 Os 26 efeitos — a fatia longa

Cada efeito é um projeto pequeno: medir → identificar estrutura → implementar → validar por null-test. Agrupar por família reduz o custo:

| Família | Efeitos | Estratégia |
|---|---|---|
| Filtros/EQ | Filter+Drive, Isolator, Lo-Fi | Base comum de biquads + saturação |
| Delay | Delay, Tape Echo | Linha de delay comum + modelagem de fita |
| Modulação | Chorus, Flanger, Phaser | Delay modulado + all-pass |
| Pitch | Pitch Shifter, Voice Transformer | Motor de pitch comum |
| Reverb | Reverb | Isolado, o mais caro individualmente |
| Rítmicos | Slicer | Gate sincronizado ao transporte |
| Dinâmica/distorção | Compressor, Distortion | Detector + curvas |
| Vinil | Vinyl Simulator | Ruído + wow/flutter + filtragem |

### 8.6 Estágio de saída

Filtro de reconstrução + resposta do estágio analógico. Modelável com um FIR curto derivado da resposta ao impulso medida. Barato e melhora o null-test de forma perceptível.

---

## 9. Integração com a DAW

Aqui moram os problemas que **não existem no hardware** e que definem se o plugin é usável de verdade.

### 9.1 Sincronismo de transporte

O sequenciador de patterns deve seguir o playhead do host (`AudioPlayHead::PositionInfo`: BPM, posição em quarter notes, play/stop, loop). Requisitos:

- Trancar o pattern à grade do host, com opção de free-run para uso ao vivo
- Sobreviver a *scrub*, salto de posição e mudança de tempo no meio do bloco
- Tap Tempo continua existindo, mas alimenta o modo free-run

### 9.2 Como fazer "sampling" dentro de uma DAW

No hardware você pluga um toca-discos. No plugin, três caminhos — e vale suportar os três:

1. **Entrada de áudio do track:** o plugin recebe o sinal do canal onde está inserido e grava com REC. Fluxo mais próximo do hardware.
2. **Sidechain input:** grava de outro canal sem interromper a saída. Mais flexível em Ableton.
3. **Drag & drop / browser:** arrastar um arquivo (ou um clip do browser do Live) direto para um pad. É o caminho que 90% dos usuários vai usar.

### 9.3 Resample dentro do plugin

Mais fácil que no hardware — o sinal interno já está disponível. Duas modalidades:

- **Tempo real (autêntico):** arma o REC, o usuário toca os pads e gira os CTRL, e o buffer interno é capturado com toda a performance de knobs. Buffer pré-alocado, sem alocação no audio thread.
- **Offline (moderno):** renderiza mais rápido que tempo real na thread de background.

### 9.4 MIDI

- **Pads → notas MIDI** (mapa configurável, padrão compatível com controladores comuns): essencial para tocar de um Push/MPD e para gravar em clips MIDI. Não existe no hardware, é adição necessária.
- **CTRL 1/2/3 → parâmetros automatizáveis** expostos ao host, para automação e MIDI learn.
- Cuidado: expor parâmetros ao host cria uma tensão com o "Authentic Mode" (o hardware não tem automação). Resolver expondo os parâmetros sempre — automação é ganho puro, sem custo de fidelidade sonora.

### 9.5 Estado e presets

- Serialização completa do `Device` (incluindo áudio dos pads) no estado do plugin, para o projeto abrir igual
- **Decisão importante:** embutir o áudio no estado do projeto (projeto pesado, mas portátil) ou referenciar arquivos externos (leve, mas quebra ao mover). Recomendação: **embutir por padrão** com opção de referência — o histórico de DAWs mostra que projetos quebrados por sample faltando são a reclamação nº 1 de samplers.
- Formato de preset próprio, exportável/importável.

### 9.6 Feature de alto valor: importar cartões SmartMedia

Ler dumps de cartões SmartMedia reais de SP-303 e carregar os bancos direto no plugin. O formato foi documentado pela comunidade. É legalmente seguro (ler um formato de dados ≠ copiar firmware) e é um diferencial enorme para quem tem o hardware.

---

## 10. Interface e usabilidade

### 10.1 Princípio

**Replicar a topologia e o fluxo, não a arte.** Mesmo layout funcional (8 pads, 3 knobs CTRL, botões de efeito dedicados, display de 3 dígitos), identidade visual própria. Isso resolve o risco de trade dress (§12) sem perder a usabilidade — porque o que faz o SP-303 ser rápido não é a cor da caixa, é a **relação fixa entre três knobs e o efeito ativo**.

### 10.2 O problema do display de 3 dígitos

O display críptico é simultaneamente parte da identidade **e** o pior aspecto de usabilidade do aparelho. Solução:

- Manter o display de 7 segmentos fiel, com as mesmas mensagens de 3 caracteres
- Adicionar uma **barra de contexto** que traduz em texto legível o que o display está mostrando e o que os CTRL controlam **naquele momento**
- No Authentic Mode, a barra pode ser desligada

Isso preserva a estética e elimina a necessidade de decorar tabela de códigos.

### 10.3 Requisitos de GUI

- **Redimensionável** e resolução-independente (vetorial ou bitmaps @3x) — obrigatório em 2026
- **Pads acionáveis** por mouse, MIDI e teclado do computador
- **Feedback visual em tempo real**: LEDs dos pads, nível, posição de reprodução dentro do sample
- **Waveform opcional** (Modern Mode) com START/END arrastáveis; escondida no Authentic Mode
- **Acessibilidade:** um plugin cuja única saída de informação é um display de 3 dígitos é inacessível a leitor de tela. As APIs de acessibilidade do JUCE devem ser usadas para expor nomes e valores de todos os controles — isso não é opcional em software moderno.

### 10.4 Fluxos que precisam ser testados com usuários

1. Do zero ao primeiro loop tocando: **meta de menos de 60 segundos**
2. Chop de um break em 4 pads
3. Aplicar filtro → resample → aplicar segundo efeito
4. Gravar um pattern de 4 compassos sincronizado ao Live

---

## 11. Testes, QA e CI/CD

### 11.1 Pirâmide de testes

| Nível | O que | Ferramenta |
|---|---|---|
| Unitário | Cada bloco de DSP isolado (resposta ao impulso, resposta em frequência, estabilidade) | Catch2 |
| Golden file | Renderizar entrada fixa por cada efeito e comparar com referência versionada, com tolerância | Catch2 + WAV commitados |
| **Null-test** | Renderizar contra as gravações do hardware da Fase 0, medir resíduo em dB, **falhar o build se regredir** | Python + numpy no CI |
| Real-time safety | Detectar alocação/lock no audio thread | `RealtimeSanitizer` / hooks de alocação em builds de debug |
| Compatibilidade | `pluginval` em nível estrito | pluginval no CI |
| Matriz de host | Ableton Live, Logic, Reaper, Bitwig, FL, Pro Tools | Manual, por release |

O **null-test como gate de CI** é o item mais importante desta lista. É o que transforma "achamos que soa parecido" em um número que não pode piorar sem alguém perceber.

### 11.2 Pipeline

```
push → build (macOS universal + Windows x64)
     → testes unitários + golden files
     → null-test vs. dataset do hardware  ── falha se resíduo > limiar
     → pluginval strict
     → assinatura de código + notarização (Apple) / assinatura EV (Windows)
     → artefatos de instalador
```

**Custos fixos de distribuição** a orçar desde o dia 1: Apple Developer Program (anual), certificado EV de Windows (anual, e o processo de emissão leva semanas), e — se AAX entrar no escopo — parceria Avid + assinatura PACE/iLok.

---

## 12. Riscos: técnicos e jurídicos

### 12.1 Jurídicos — tratar antes de escrever a primeira linha

| Risco | Gravidade | Mitigação |
|---|---|---|
| **Marca "SP-303" / "Boss" / "Roland" no nome ou marketing** | Alta | Nome próprio. Referência factual comparativa só se necessária, sem usar as marcas como identidade do produto |
| **Trade dress** (copiar a aparência do painel) | Alta | Layout funcional equivalente, identidade visual original (§10.1) |
| **Dump / port de firmware** | Muito alta | **Proibido.** Só engenharia reversa comportamental de caixa-preta. Documentar a metodologia para provar procedência limpa |
| **Samples de fábrica do hardware** | Média | Não redistribuir. Criar biblioteca própria, gravada do zero |
| **Licença VST3 / JUCE** | Média | VST3 SDK exige aceitar o acordo Steinberg (GPLv3 ou proprietário). JUCE: GPL ou comercial — verificar o tier atual antes do commit inicial |

> Vale registrar: fabricantes de hardware historicamente agem contra emulações que usam suas marcas e aparência. O caminho seguro e bem trilhado é **clone funcional com identidade própria**.

### 12.2 Técnicos

| Risco | Impacto | Mitigação |
|---|---|---|
| **Acesso ao hardware para medição** | Bloqueia a Fase 0 inteira | Garantir a unidade antes de iniciar. Sem hardware, o teto de qualidade cai para L1 |
| **Os 26 efeitos estouram o cronograma** | Alto — é a fatia longa | Priorizar os 5 diretos + Reverb/Tape Echo/Slicer no MVP; entregar o resto incrementalmente |
| **Ambiguidade do time-stretch** (preserva pitch ou não) | Médio — muda o algoritmo | Resolver na Fase 0 por medição, antes de projetar o motor de voz |
| **Alocação acidental no audio thread** | Crítico em produção, invisível em dev | RealtimeSanitizer no CI desde o primeiro commit |
| **Divergência de comportamento entre hosts** | Médio | pluginval + matriz de hosts a cada release |

---

## 13. Roadmap, esforço e complexidade

Estimativas para **um desenvolvedor sênior de áudio** (C++/JUCE/DSP). Semanas-pessoa.

| Fase | Escopo | Semanas |
|---|---|---|
| **0. Medição** | Aquisição do hardware, protocolo, captura, identificação de sistema, dataset + relatório | **3–4** |
| **1. Núcleo de DSP** | Biblioteca headless: interpolação, decimação, quantização, motor de voz, 8 vozes, modos de disparo, testes | **4–6** |
| **2. Shell do plugin** | JUCE, VST3+AU, estado, params, GUI mínima funcional, carregar samples, tocar | **3–4** |
| **3. Sequenciador + Resample** | Patterns, quantize, swing, sync de host, captura em tempo real e offline | **4–5** |
| **4. Efeitos (26)** | Medir → identificar → implementar → validar, por família | **10–16** |
| **5. GUI / UX final** | Painel completo, display 7-seg, waveform, redimensionamento, acessibilidade, testes de usabilidade | **5–8** |
| **6. QA e release** | Matriz de hosts, pluginval, instaladores, assinatura, notarização, docs | **4–6** |
| | **Total** | **33–49 semanas ≈ 8–12 meses** |

### 13.1 Compressão com time

Com **3 pessoas** (1 DSP, 1 plugin/infra, 1 GUI/UX), as fases 4 e 5 paralelizam com a 3:

**Calendário realista: ~4–5 meses** para o produto completo.

### 13.2 Onde o esforço realmente está

Um erro comum é achar que a dificuldade está em "fazer soar lo-fi". Não está:

- **~35%** — os 26 efeitos (trabalho repetitivo mas irredutível)
- **~20%** — GUI e usabilidade (sempre subestimado)
- **~15%** — medição e identificação (o que determina se o resultado é L1 ou L2)
- **~15%** — integração com DAW: sync, estado, MIDI, compatibilidade entre hosts
- **~10%** — motor de voz e sequenciador
- **~5%** — build, assinatura, distribuição

---

## 14. Definição de MVP

Um recorte **coerente e utilizável**, não uma demo quebrada. **~10–12 semanas solo.**

**Dentro:**

- 8 pads × 4 bancos, carregamento por drag & drop e file browser
- Os 3 modos de qualidade (Standard / Long / Lo-Fi) com decimação medida
- Modos de disparo completos: Trigger/Gate × Loop/OneShot × Normal/Reverse × Hold
- START / END / LEVEL por pad
- **5 efeitos diretos:** Filter+Drive, Pitch, Delay, Vinyl Sim, Isolator — com CTRL 1/2/3
- **Resample em tempo real** (é o que define o fluxo do aparelho — não pode ficar de fora)
- Pads mapeados para MIDI; CTRL automatizáveis
- GUI funcional redimensionável com display de 3 dígitos + barra de contexto
- VST3 + AU, macOS universal + Windows x64
- Estado de projeto persistente com áudio embutido

**Fora do MVP (fase 2):**

- Sequenciador de patterns (adia para depois — na DAW, o usuário tem o sequenciador do host)
- Os 21 efeitos MFX restantes
- Importação de cartão SmartMedia
- Time-stretch (depende do resultado da medição)
- CLAP, AAX

**Justificativa do recorte:** o MVP entrega o *loop criativo central* — carregar, chopar, filtrar, resamplear — que é o que faz o SP-303 ser o SP-303. O sequenciador é a parte mais facilmente substituída pela própria DAW, e por isso é a primeira a sair.

---

## Próximo passo concreto

Antes de qualquer código: **confirmar acesso a uma unidade SP-303 física**. Toda a diferença entre um plugin lo-fi genérico e uma emulação verificável está na Fase 0, e ela é impossível sem o hardware. Se o hardware não estiver disponível, a conversa muda — e vale decidir conscientemente por um alvo L1 com expectativas ajustadas.

---

*Documento de proposta de engenharia. Estimativas baseadas em escopo típico de projetos de emulação de sampler; assumem desenvolvedor sênior com experiência prévia em JUCE e DSP em tempo real. Requisitos de licenciamento de SDKs e termos de frameworks devem ser verificados na data de início do projeto.*

| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-H21 | ESP32-H4 | ESP32-P4 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | --------- | -------- | -------- | -------- | -------- |

# Genius (Simon) na ESP32 com UART

Implementação do clássico jogo de memória **Genius** (Simon) em uma ESP32 usando ESP-IDF.
A ESP32 acende uma sequência de LEDs coloridos e o jogador precisa repetir essa sequência
digitando os números correspondentes no monitor serial (UART). A cada acerto, a sequência
cresce em um passo; ao errar, o jogo acaba e reinicia.

## Funcionamento

1. A cada nível, o jogo sorteia uma nova cor e a adiciona ao final da sequência.
2. A sequência completa é exibida piscando os LEDs (mensagem `Observe a sequencia...`).
3. O jogador repete a sequência digitando `1`, `2`, `3` ou `4` no terminal serial.
   Cada tecla acende o LED correspondente como feedback visual.
4. **O erro é acusado imediatamente**: assim que o jogador digita uma cor que não bate com
   a sequência, o jogo encerra a rodada (`*** ERRADO! Game Over! ***`) sem esperar o resto
   dos dígitos, reproduz uma animação de game over e reinicia do nível 1.
5. Se a sequência for repetida corretamente até o fim, avança para o próximo nível.
6. Ao completar `MAX_SEQUENCE` (20) níveis, o jogador vence (`*** PARABENS! ***`).

### Limite de tempo da resposta

O jogador tem um tempo máximo de **inatividade** entre uma tecla e outra. A contagem é
**reiniciada a cada tecla digitada**, então o jogo só encerra por tempo esgotado
(`*** TEMPO ESGOTADO! Game Over! ***`) se o jogador ficar parado, sem apertar nenhuma tecla,
por mais que o limite — nunca no meio de uma digitação em andamento.

O limite é definido pela macro `RESPONSE_TIMEOUT_MS` (em milissegundos) no topo de
`main/uart.c`. O valor padrão é `10000` (10 s). O limite é informado ao jogador na
mensagem `Sua vez! Repita a sequencia (em ate N s):`.

```c
#define RESPONSE_TIMEOUT_MS  10000   // 10 segundos para responder
```

### Mapeamento das cores

| Tecla | Cor      | GPIO  |
| ----- | -------- | ----- |
| `1`   | Azul     | GPIO4 |
| `2`   | Verde    | GPIO5 |
| `3`   | Vermelho | GPIO6 |
| `4`   | Amarelo  | GPIO7 |

## Arquitetura do código

O projeto (`main/uart.c`) usa duas tasks do FreeRTOS coordenadas por semáforos:

- **`game_task`** — controla o fluxo do jogo: sorteia a sequência, exibe os LEDs, imprime
  as mensagens e decide acerto/erro/vitória a cada nível.
- **`uart_rx_task`** — é a **única** dona da leitura da UART. Lê as teclas, dá o feedback
  visual e compara cada dígito com a sequência em tempo real, sinalizando o resultado para
  a `game_task`.

Mecanismos de sincronização:

- `input_done_sem` — binário; sinaliza à `game_task` que a vez do jogador terminou (por
  acerto completo ou por erro).
- `uart_mutex` — serializa as escritas na UART para que mensagens de tasks diferentes não
  se misturem.
- Flags voláteis: `input_allowed` (libera/bloqueia a leitura de teclas), `start_input`
  (`game_task` pede à `uart_rx_task` que limpe o buffer e libere o input) e `input_error`
  (indica que o jogador errou).

> **Observação:** o `uart_flush_input()` é chamado dentro da `uart_rx_task`, e não na
> `game_task`. Como as duas tasks dividem a mesma UART, chamar o flush de uma task enquanto
> a outra está em `uart_read_bytes()` disputa o mutex interno de RX do driver e trava o
> jogo. Centralizar toda a leitura/flush em uma só task evita esse impasse.

## Hardware necessário

- Uma placa de desenvolvimento baseada em ESP32.
- 4 LEDs (azul, verde, vermelho, amarelo) com seus resistores limitadores (ex.: 220–330 Ω),
  conectados aos GPIOs 4, 5, 6 e 7, com o terminal negativo no GND.
- Cabo USB para gravação e monitoramento serial.

## Como compilar e gravar

Com o ESP-IDF instalado e o ambiente configurado:

```
idf.py -p PORT flash monitor
```

(Para sair do monitor serial, tecle `Ctrl-]`.)

A UART usada é a `UART_NUM_0` (a mesma do monitor), a 115200 bauds.

## Exemplo de saída

```
=== GENIUS ESP32 ===
1=Azul  2=Verde  3=Vermelho  4=Amarelo

--- Nivel 1 ---
Observe a sequencia...
Sua vez! Repita a sequencia:
[1/1] Vermelho
Correto! Proximo nivel...

--- Nivel 2 ---
Observe a sequencia...
Sua vez! Repita a sequencia:
[1/2] Azul
[2/2] Verde
Correto! Proximo nivel...
```

Caso o jogador erre uma cor no meio da sequência:

```
--- Nivel 3 ---
Observe a sequencia...
Sua vez! Repita a sequencia:
[1/3] Azul
[2/3] Amarelo

*** ERRADO! Game Over! ***
Reiniciando...
```

# Steam Big Picture para CrossMix

Versao 0.8.4 de um launcher com a aparencia do Big Picture, baseado nas
capturas fornecidas: inicio com carrossel, biblioteca de capas, menu lateral,
favoritos, colecoes, busca, aplicativos e ajustes de video/standby.

O Big Picture completo é um aplicativo SDL2 independente: aparece em **Apps > Steam Big Picture** e pode ser escolhido como interface ao ligar. O projeto inclui o código-fonte e o binário ARM64 para CrossMix.

## Prévia desktop

O binário ARM64 é usado no TrimUI. No PC, o mesmo código C++ compila para x86_64 e abre uma janela SDL2 pelo Linux/WSLg. No Windows com WSLg, instale as dependências descritas em [`desktop/README.md`](desktop/README.md) e execute `Apps/BigPicture/desktop/run-desktop.ps1` na raiz do checkout. A primeira abertura compila o executável local.

A prévia lê o catálogo do CrossMix e mantém as preferências da sessão em `desktop/.runtime`. A listagem de jogos depende de `Emus`, `Roms`, `Best`, `Imgs` e `Icons` disponíveis no checkout. O lançamento de jogos e funções específicas do aparelho podem exigir emuladores ou hardware do TrimUI.

## Onde guardar seus videos

No checkout local:

`Data/BigPicture/videos/startup`

No cartao:

`Data/BigPicture/videos/startup`

Essa é a pasta para adicionar MP4 ou WebM. Para o aparelho, prefira MP4 H.264, 1280x720, yuv420p e áudio AAC. O repositório inclui apenas dois exemplos leves: `steam-big-picture-classico.mp4` e `steam-pessoal-01.mp4`. Copie outros vídeos manualmente para essa pasta e reinicie o aplicativo para relê-los; os demais arquivos do cache Steam não são incluídos.

Em **Videos de inicio**, L/R (F6/F7 no PC) alterna os tres modos:

- **Aleatorio: todos**: usa todos os videos da pasta.
- **Aleatorio: marcados**: X (Shift/F9 no PC) marca ou desmarca cada video.
  Somente os marcados participam; sem marcacoes a introducao e pulada.
- **Um video fixo**: X escolhe o video que sera sempre apresentado.

A/Enter reproduz uma previa sem mudar a escolha. Cima no primeiro video leva
ao seletor de modo; baixo retorna a lista. As preferencias sao salvas para
o inicio e para o retorno do standby. Qualquer botao, direcao ou clique pula
a animacao; o comando e consumido para evitar ativar algo no menu ao retornar.
Em **Configuracoes**, tambem e possivel escolher o modo, ativar/desativar a
introducao no inicio e no retorno e ajustar o volume.

## Tela apagada e retorno

Padrao: 120 segundos de inatividade, retorno por qualquer botao, introducao no
inicio e no retorno. Em Configuracoes, o tempo pode ser desligado ou definido
em 30, 60, 120, 300 ou 600 segundos. As politicas de retorno sao qualquer botao,
Power e Fn.

Com a interface acordada, um toque no botao Power apaga a tela sem encerrar o
launcher. Outro toque em Power restaura a tela e reproduz uma introducao usando
a selecao atual: sorteio de todos, sorteio dos marcados ou video fixo. Power tambem acorda o aparelho mesmo se a politica
de despertar estiver configurada para outro botao. O interruptor Fn desligado
continua exigindo que o Fn seja ligado novamente.

O modo implementado e **standby de tela**: o launcher para de redesenhar, reduz
o trabalho enquanto espera entrada e, no TrimUI, envia brilho zero ao controle
de backlight do firmware. Ao retornar, restaura o brilho salvo e reproduz a
introducao antes de devolver o controle ao menu. A retomada redesenha a interface depois de acender a tela e espera o display estabilizar antes de iniciar o player; se ele falhar nessa primeira tentativa, tenta o mesmo video mais uma vez.
No PC, apenas a janela fica
preta; o monitor e o Windows nao sao suspensos.

Se o standby foi provocado pelo Fn desligado, os outros botoes nao o desfazem:
e necessario ligar o Fn novamente. Isso corresponde ao comportamento pedido
para o interruptor inferior. F10 simula esse fluxo no PC.

Isso **nao e suspensao profunda do processador**. Nao se promete retorno de
`mem` por qualquer botao ou pelo Fn: isso depende das fontes de despertar do
kernel. Economia real de bateria, brilho e entrada fisica exigem teste no
TrimUI. O standby desta versao atua enquanto o launcher esta ativo; durante
um jogo, continuam valendo os controles de energia do firmware/emulador.

## Instalar no CrossMix

O binário ARM64 já está incluído em `Apps/BigPicture/bin/bigpicture`. Para copiar manualmente para um cartão CrossMix, preserve a árvore e copie:

- `Apps/BigPicture`;
- `Data/BigPicture`.

A variação de cores do MainUI não faz parte desta integração. O app também possui opções de inicialização automática e registro Fn, mas elas não foram ativadas nem validadas no TrimUI nesta etapa; o foco atual é a prévia desktop.

## Arquivos para modificar

- `src/main.cpp`: interface, eventos, videos e estado de standby;
- `src/console_art.hpp`: recorte por transparencia, contorno e sombra dos consoles;
- `consoles/<ID>.png`: imagens transparentes dos aparelhos, substituiveis;
- `catalog.py`: leitura dos jogos, imagens e aplicativos do CrossMix;
- `power.sh`: controle do backlight no console e simulacao no PC;
- `com.crossmix.bigpicture.sh`: entrada do Fn;
- `Data/BigPicture/settings.json`: preferencias;
- `Data/BigPicture/library-state.json`: favoritos, recentes e tempo de jogo;
- `Data/BigPicture/events.jsonl`: registro das transicoes;
- `Data/BigPicture/runtime-status.json`: estado atual da previa.

O fornecedor JSON (nlohmann 3.11.3) e a fonte DejaVu acompanham suas licencas.

O player no TrimUI usa as saidas SDL de video e audio. A opcao OSC fica
restrita ao PC porque o mpv 0.32 do firmware testado nao inclui esse recurso.

## Alcance da validacao

Compilacao x86_64 e ARM64, carregamento ARM64 no QEMU, politicas de retorno,
catalogacao e fluxos de video/standby sao conferidos no PC. Inicializacao do
cartao, acionamento do Fn no firmware, backlight, consumo de bateria e
emuladores precisam de validacao fisica. O launcher e uma primeira versao
funcional inspirada na referencia; nao inclui os servicos online da Steam.

### Retorno do standby (atualizacao 0.8.5)

O aplicativo aparece como **Big Picture**. Ao apagar a tela, o player prepara
um video pausado; ao despertar, a iluminacao volta depois que o mpv informa a
posicao inicial. Se o player falhar ou nao ficar pronto em cinco segundos, a
interface reaparece com aviso. Se o video de retorno estiver desativado, a
interface volta diretamente. O comportamento do player, do framebuffer e da
iluminacao ainda precisa ser confirmado no TrimUI fisico.

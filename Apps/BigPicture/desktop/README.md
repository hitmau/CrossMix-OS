# Prévia desktop

O binário ARM64 do aplicativo roda no TrimUI. Para testar a mesma interface no PC, estes scripts compilam o mesmo `src/main.cpp` para x86_64 e abrem uma janela SDL2 no Linux ou no WSLg.

## Windows com WSLg

No Ubuntu do WSL, instale as dependências uma vez:

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev python3 mpv
```

No PowerShell, na raiz do checkout:

```powershell
.\Apps\BigPicture\desktop\run-desktop.ps1
```

O script usa `D:\Trimui\CrossMix-OS-main` como referência quando essa pasta existe; assim o menu desktop pode ler o catálogo local sem duplicar os dados. Para indicar outro checkout, passe o caminho como parâmetro. Sem referência externa, a prévia usa a raiz do fork. O primeiro início compila o executável x86_64. Para recompilar depois de editar o código, apague `desktop/bigpicture-pc` ou rode `desktop/build.sh` no WSL.

A prévia cria a área de execução em `desktop/.runtime`, guarda ali as preferências locais e mantém os dados do CrossMix de referência vinculados por symlinks. Ela não altera firmware nem configuração de boot. O lançamento de jogos e funções específicas do TrimUI continua dependendo do suporte de cada emulador no PC. O WSLg precisa estar ativo para mostrar a janela.

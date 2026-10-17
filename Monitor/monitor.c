#include "../comum.h"
#include "resource.h"

typedef struct {
    SHM_ALERTA shmLocal;     // Cópia local dos alertas para desenhar
    HANDLE hMapFile;         // Handle da memória partilhada
    SHM_ALERTA* pBuf;        // Ponteiro para a memória partilhada
    HANDLE hEventUpdate;     // Evento para a central avisar o monitor
    HANDLE hMutex;           // Mutex para ler a SHM com segurança
    HANDLE hThread;          // Thread que fica à escuta da central
    HWND hWnd;               // Handle da própria janela
    BOOL continua;           // Flag de controlo da thread

    // Configurações e paginação
    int maxAlertas;          // Limite visual
    int paginaAtual;         // Para o Page Up / Page Down
    TCHAR nomeSHM[100];
    TCHAR nomeMutex[100];
    TCHAR nomeEvento[100];
} DATA;

#define MAX_ALERTAS 5

LRESULT CALLBACK trataEventos(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK TrataConfig(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

TCHAR szProgName[] = TEXT("Monitor");

// ============================================================================
// THREAD: Fica à espera que a Central avise que há alterações na SHM
DWORD WINAPI escutaCentral(LPVOID dados) {
    DATA* ptd = (DATA*)dados;

    while (ptd->continua) {
        // 1. Fica à espera do sinal da Central
        DWORD resEvento = WaitForSingleObject(ptd->hEventUpdate, INFINITE);

        if (resEvento == WAIT_FAILED || resEvento == WAIT_ABANDONED) {
            PostMessage(ptd->hWnd, WM_CLOSE, 0, 0);
            break;
        }

        if (!ptd->continua) break;

        // Criamos uma flag para saber se algo mudou nesta rodada
        BOOL mudouAlgo = FALSE;

        DWORD resMutex = WaitForSingleObject(ptd->hMutex, INFINITE);
        if (resMutex == WAIT_OBJECT_0) {
            if (ptd->pBuf != NULL) {
                // DETEÇÃO DE MUDANÇAS: Comparamos a SHM com a nossa cópia local
                // Se o conteúdo mudou (entrou placar, saiu placar, mudou msg ou desligou)
                if (memcmp(&(ptd->shmLocal), ptd->pBuf, sizeof(SHM_ALERTA)) != 0) {
                    // Copiamos os novos dados para a memória local
                    CopyMemory(&(ptd->shmLocal), ptd->pBuf, sizeof(SHM_ALERTA));
                    mudouAlgo = TRUE; // Ativamos a flag!
                }
            }
            ReleaseMutex(ptd->hMutex);
        }
        else if (resMutex == WAIT_FAILED || resMutex == WAIT_ABANDONED) {
            PostMessage(ptd->hWnd, WM_CLOSE, 0, 0);
            break;
        }

        // SÓ REPINTA SE HOUVE MUDANÇA REAL!
        // Usamos TRUE para limpar o lixo antigo, mas como só acontece UMA vez,
        // o ecrã não fica a piscar
        if (mudouAlgo) {
            InvalidateRect(ptd->hWnd, NULL, TRUE);
        }

        if (ptd->shmLocal.desligar == TRUE) {
            PostMessage(ptd->hWnd, WM_CLOSE, 0, 0);
            break;
        }
    }
    return 0;
}


// ============================================================================
// WINMAIN: Criação e registo da Janela
// ============================================================================
int WINAPI _tWinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPTSTR lpCmdLine, int nCmdShow) {
    HWND hWnd;
    MSG lpMsg;
    WNDCLASSEX wcApp;
    DATA d;

    // Inicializar estrutura
    ZeroMemory(&d, sizeof(DATA));
    d.maxAlertas = MAX_ALERTAS; // Valor por defeito
    d.paginaAtual = 0;

    wcApp.cbSize = sizeof(WNDCLASSEX);
    wcApp.hInstance = hInst;
    wcApp.lpszClassName = szProgName;
    wcApp.lpfnWndProc = trataEventos;
    wcApp.style = CS_HREDRAW | CS_VREDRAW;
    wcApp.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcApp.hIconSm = LoadIcon(NULL, IDI_INFORMATION);
    wcApp.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcApp.lpszMenuName = MAKEINTRESOURCE(IDR_MENU1); 
    wcApp.cbClsExtra = 0;
    wcApp.cbWndExtra = sizeof(DATA*); 
    wcApp.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);

    if (!RegisterClassEx(&wcApp)) return(0);


    // VALIDAÇÃO ANTES DE CRIAR A JANELA
    _tcscpy_s(d.nomeSHM, _countof(d.nomeSHM), NOME_SHM);
    _tcscpy_s(d.nomeMutex, _countof(d.nomeMutex), NOME_MUTEX);
    _tcscpy_s(d.nomeEvento, _countof(d.nomeEvento), NOME_EVENTO_UPDATE);

    d.hMapFile = OpenFileMapping(FILE_MAP_READ, FALSE, d.nomeSHM);
    d.hMutex = OpenMutex(MUTEX_ALL_ACCESS, FALSE, d.nomeMutex);
    d.hEventUpdate = OpenEvent(EVENT_ALL_ACCESS, FALSE, d.nomeEvento);

    if (d.hMapFile == NULL || d.hMutex == NULL || d.hEventUpdate == NULL) {
        MessageBox(NULL,
            _T("Erro: Nao foi possivel ligar a Central.\nGaranta que a Central esta a correr primeiro!"),
            _T("Erro de Arranque"),
            MB_OK | MB_ICONERROR);

        if (d.hMapFile) CloseHandle(d.hMapFile);
        if (d.hMutex) CloseHandle(d.hMutex);
        if (d.hEventUpdate) CloseHandle(d.hEventUpdate);

        return 0; // Termina o programa
    }

    // Mapeamento da memória também feito aqui de forma segura
    d.pBuf = (SHM_ALERTA*)MapViewOfFile(d.hMapFile, FILE_MAP_READ, 0, 0, sizeof(SHM_ALERTA));
    if (d.pBuf == NULL) {
        CloseHandle(d.hMapFile); CloseHandle(d.hMutex); CloseHandle(d.hEventUpdate);
        return 0;
    }

    hWnd = CreateWindow(szProgName, TEXT("Monitor de Alertas"), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 400,
        (HWND)HWND_DESKTOP, (HMENU)NULL, (HINSTANCE)hInst, (LPVOID)&d);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    while (GetMessage(&lpMsg, NULL, 0, 0) > 0) {
        TranslateMessage(&lpMsg);
        DispatchMessage(&lpMsg);
    }
    return (int)lpMsg.wParam;
}

// ============================================================================
// FUNÇÃO DE PROCESSAMENTO (CALLBACK)
// ============================================================================
LRESULT CALLBACK trataEventos(HWND hWnd, UINT messg, WPARAM wParam, LPARAM lParam) {
    DATA* pd = NULL;
    HDC hdc;
    PAINTSTRUCT ps;

    switch (messg) {
    case WM_CREATE:
        pd = (DATA*)((LPCREATESTRUCT)lParam)->lpCreateParams;
        pd->hWnd = hWnd;
        pd->continua = TRUE;

        SetWindowLongPtr(hWnd, 0, (LONG_PTR)pd);

        if (pd->pBuf && pd->hMutex) {
            WaitForSingleObject(pd->hMutex, INFINITE);
            CopyMemory(&pd->shmLocal, pd->pBuf, sizeof(SHM_ALERTA));
            ReleaseMutex(pd->hMutex);
        }

        // Lança a thread de escuta
        if (pd->hEventUpdate) {
            pd->hThread = CreateThread(NULL, 0, escutaCentral, pd, 0, NULL);
        }
        break;

    case WM_PAINT:
        pd = (DATA*)GetWindowLongPtr(hWnd, 0);
        hdc = BeginPaint(hWnd, &ps);

        if (pd != NULL) {
            int yPos = 20; // Posição vertical inicial
            int countAlertas = 0; // Conta quantos placares já avaliámos que têm alertas
            int indexInicio = pd->paginaAtual * pd->maxAlertas; // A partir de que alerta começamos a desenhar
            int desenhadosNestaPagina = 0;
            TCHAR linhaTexto[200];

            // Cabeçalho
            TextOut(hdc, 20, yPos, _T("--- ALERTAS ATIVOS ---"), 22);
            yPos += 30;

            // Percorrer os placares para encontrar os que têm alertas ativos
            for (int i = 0; i < MAX_PLACARES; i++) {
                // Se o placar existir (identificador > 0) e tiver uma mensagem não vazia (tem um alerta)
                if (pd->shmLocal.placar[i].identificador > 0 && _tcslen(pd->shmLocal.placar[i].msg) > 0) {

                    // Se este alerta pertence à página que estamos a ver atualmente
                    if (countAlertas >= indexInicio && desenhadosNestaPagina < pd->maxAlertas) {
                        _stprintf_s(linhaTexto, _countof(linhaTexto), _T("Placar ID: %u | Duração: %u s | Msg: %s"),
                            pd->shmLocal.placar[i].identificador,
                            pd->shmLocal.placar[i].duracao,
                            pd->shmLocal.placar[i].msg);

                        TextOut(hdc, 20, yPos, linhaTexto, _tcslen(linhaTexto));
                        yPos += 25; // Descer para a próxima linha
                        desenhadosNestaPagina++;
                    }
                    countAlertas++; // Encontrámos um alerta, aumentamos a contagem geral
                }
            }

            // Se não encontrou nenhum alerta no total
            if (countAlertas == 0) {
                TextOut(hdc, 20, yPos, _T("Nao ha alertas ativos no momento."), 33);
            }
            else {
                // Info da paginação no fundo
                yPos += 20;
                _stprintf_s(linhaTexto, _countof(linhaTexto), _T("Pagina %d (Total de alertas: %d)"), pd->paginaAtual + 1, countAlertas);
                TextOut(hdc, 20, yPos, linhaTexto, _tcslen(linhaTexto));



                int totalPaginasNecessarias = (countAlertas + pd->maxAlertas - 1) / pd->maxAlertas;

                yPos += 20; // Desce mais uma linha para o aviso

                if (pd->paginaAtual + 1 < totalPaginasNecessarias) {
                    // Se não estamos na última página, avisa que há mais à frente!
                    TextOut(hdc, 20, yPos, _T("-> Carregue em [Page Down] para ver a proxima pagina."), 52);
                }
                else if (pd->paginaAtual > 0 && pd->paginaAtual + 1 == totalPaginasNecessarias) {
                    // Se estamos na última página mas viemos de trás, avisa que pode voltar
                    TextOut(hdc, 20, yPos, _T("<- Carregue em [Page Up] para voltar a pagina anterior."), 54);
                }
            }
        }

        EndPaint(hWnd, &ps);
        break;

    case WM_KEYDOWN:
        pd = (DATA*)GetWindowLongPtr(hWnd, 0);

        

        if (wParam == VK_NEXT) { // Page Down
            int totalAlertasAtivos = 0;
            for (int i = 0; i < MAX_PLACARES; i++) {
                if (pd->shmLocal.placar[i].identificador > 0 && _tcslen(pd->shmLocal.placar[i].msg) > 0) {
                    totalAlertasAtivos++;
                }
            }

            int totalPaginas = (totalAlertasAtivos + pd->maxAlertas - 1) / pd->maxAlertas;
            if (totalPaginas < 1) totalPaginas = 1;

            // Só deixa avançar se a próxima página não ultrapassar o total de páginas existente
            if (pd->paginaAtual + 1 < totalPaginas) {
                pd->paginaAtual++;
                InvalidateRect(hWnd, NULL, TRUE); 
            }
        }
        else if (wParam == VK_PRIOR) { // Page Up
            // Só deixa recuar se não estivermos já na primeira página (Página 0)
            if (pd->paginaAtual > 0) {
                pd->paginaAtual--;
                InvalidateRect(hWnd, NULL, TRUE);
            }
        }
        break;

    case WM_DESTROY:
        pd = (DATA*)GetWindowLongPtr(hWnd, 0);

        if (pd != NULL) {
            pd->continua = FALSE; // Sinaliza à thread para parar

            // Acorda o evento caso ele ainda exista
            if (pd->hEventUpdate) SetEvent(pd->hEventUpdate);

            // CORREÇÃO CRÍTICA DO DEADLOCK: 
            // Em vez de INFINITE, esperamos apenas 50ms. Se a thread estiver presa 
            // num deadlock do Kernel, a thread principal avança e limpa a memória RAM!
            if (pd->hThread) {
                DWORD waitThr = WaitForSingleObject(pd->hThread, 50);
                CloseHandle(pd->hThread);
            }

            // ============================================================
            // LIBERTAR TUDO DO KERNEL DO WINDOWS (Aqui limpas as amarras!)
            // ============================================================
            if (pd->pBuf) {
                UnmapViewOfFile(pd->pBuf);
                pd->pBuf = NULL;
            }
            if (pd->hMapFile) {
                CloseHandle(pd->hMapFile);
                pd->hMapFile = NULL;
            }
            if (pd->hMutex) {
                CloseHandle(pd->hMutex);
                pd->hMutex = NULL;
            }
            if (pd->hEventUpdate) {
                CloseHandle(pd->hEventUpdate);
                pd->hEventUpdate = NULL;
            }
        }

        // Aviso final solto no ecrã (Pai = NULL) para o utilizador saber que fechou tudo de forma limpa
        MessageBox(NULL,
            _T("O Monitor limpou todos os recursos do Windows e encerrou com sucesso."),
            _T("Encerramento Seguro"),
            MB_OK | MB_ICONINFORMATION);

        PostQuitMessage(0);
        break;
    case WM_COMMAND:
        pd = (DATA*)GetWindowLongPtr(hWnd, 0);
        if (pd == NULL) break;

        switch (LOWORD(wParam)) {

        case ID_M_CONFIGURACAO:
        {
            // Abre a DialogBox passando o ponteiro "pd" e guarda o resultado do Windows
            INT_PTR res = DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_CONFIG_DIALOG), hWnd, TrataConfig, (LPARAM)pd);

            // Se correu bem, o Windows retorna IDOK (se clicaste OK) ou IDCANCEL (se cancelaste)
            if (res == IDOK) {
                // Força a janela principal a apagar o ecrã e redesenhar os alertas com o novo limite!
                InvalidateRect(hWnd, NULL, TRUE);
            }
            // Se o resultado for -1 ou 0, significa que a Dialog falhou a abrir por erro de IDs ou recursos!
            else if (res == -1 || res == 0) {
                DWORD erroReal = GetLastError();
                TCHAR msgErro[100];
                _stprintf_s(msgErro, 100, _T("A Dialog falhou! Retorno: %d | Erro do Windows: %lu"), res, erroReal);
                MessageBox(hWnd, msgErro, _T("Debug Erro"), MB_OK | MB_ICONERROR);
            }
        }
        break;

        case ID_M_ACERCA:
            // REQUISITO M3: Caixa de informações com os dados do grupo 
            MessageBox(hWnd,
                _T("Sistemas Operativos 2 - 2025/26\n\nAutores:\n- Guilherme Eça (2024143849)\n- Miguel Zara (2024143873)"),
                _T("Acerca do Grupo"),
                MB_OK | MB_ICONINFORMATION);
            break;

        case ID_M_SAIR:
            // REQUISITO M3: Destrói a janela principal, libertando os recursos 
            DestroyWindow(hWnd);
            break;
        }
        break;
    default:
        return DefWindowProc(hWnd, messg, wParam, lParam);
    }
    return 0;
}

INT_PTR CALLBACK TrataConfig(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static DATA* pd = NULL;

    switch (message) {
    case WM_INITDIALOG:
        pd = (DATA*)lParam;
        if (pd != NULL) {
            SetDlgItemInt(hDlg, IDC_EDIT3, pd->maxAlertas, FALSE);
            SetDlgItemText(hDlg, IDC_EDIT1, NOME_SHM);
            SetDlgItemText(hDlg, IDC_EDIT4, NOME_MUTEX);
            SetDlgItemText(hDlg, IDC_EDIT2, NOME_EVENTO_UPDATE);
        }
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            if (pd != NULL) {
                pd->maxAlertas = GetDlgItemInt(hDlg, IDC_EDIT3, NULL, FALSE);
                if (pd->maxAlertas <= 0) pd->maxAlertas = 5;

                TCHAR novoSHM[100], novoMutex[100], novoEvento[100];
                GetDlgItemText(hDlg, IDC_EDIT1, novoSHM, _countof(novoSHM));
                GetDlgItemText(hDlg, IDC_EDIT4, novoMutex, _countof(novoMutex));
                GetDlgItemText(hDlg, IDC_EDIT2, novoEvento, _countof(novoEvento));

                BOOL nomesIguais = (_tcscmp(novoSHM, NOME_SHM) == 0 &&
                    _tcscmp(novoMutex, NOME_MUTEX) == 0 &&
                    _tcscmp(novoEvento, NOME_EVENTO_UPDATE) == 0);

                if (!nomesIguais) {
                    // Tentar abrir com os novos nomes
                    HANDLE hMapTeste = OpenFileMapping(FILE_MAP_READ, FALSE, novoSHM);
                    HANDLE hMutexTeste = OpenMutex(MUTEX_ALL_ACCESS, FALSE, novoMutex);
                    HANDLE hEventoTeste = OpenEvent(EVENT_ALL_ACCESS, FALSE, novoEvento);

                    if (hMapTeste && hMutexTeste && hEventoTeste) {
                        // Funcionou — mas não fazemos nada, central é sempre a mesma
                        CloseHandle(hMapTeste);
                        CloseHandle(hMutexTeste);
                        CloseHandle(hEventoTeste);
                        // (poderias aqui guardar os nomes se quisesses)
                    }
                    else {
                        // Falhou — fechar o que abriu e avisar
                        if (hMapTeste)   CloseHandle(hMapTeste);
                        if (hMutexTeste) CloseHandle(hMutexTeste);
                        if (hEventoTeste)CloseHandle(hEventoTeste);

                        MessageBox(hDlg,
                            _T("Recursos não encontrados."),
                            _T("Erro"), MB_ICONWARNING);

                        // Repor campos com os nomes originais
                        // SetDlgItemText(hDlg, IDC_EDIT1, NOME_SHM);
                        // SetDlgItemText(hDlg, IDC_EDIT4, NOME_MUTEX);
                        // SetDlgItemText(hDlg, IDC_EDIT2, NOME_EVENTO_UPDATE);

                        return (INT_PTR)FALSE;
                    }
                }
            }
            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        }
        else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
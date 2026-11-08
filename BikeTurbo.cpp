// BikeTurbo.asi - GTA San Andreas PC v1.0 (US)
//
// Digitar BIKETURBO durante o jogo liga/desliga o "turbo de bicicleta". Nao usa o sistema de
// cheats do jogo (buffer proprio; nao conta como cheat usado nem desativa conquistas/saves).
// Ligado, segurar W (ou o botao de acelerar do controle, ex.: A) em cima de uma BICICLETA faz o
// jogo registrar o botao sendo apertado varias vezes seguidas (toques reais no estado do CPad).
//
// NOVO: opcao "Bike Turbo" no menu Controller Setup (abaixo de Mouse Settings), Yes/No, que
// liga/desliga o mesmo turbo. Textos customizaveis no .ini (com suporte a caracteres como s-caron).
//
// Confirmado no gta_sa.exe 1.0 US (versao original do mod):
//  - CHud::SetHelpMessage = 0x588BE0 (cdecl: texto, quick, permanent, addToBrief).
//  - CHud::DrawHelpMessage = 0x58B6E0, chamada uma vez por frame em 0x58FCFA (CHud::Draw).
//  - CGame::Process = 0x53BEE0. Sua primeira chamada (0x53BEE6) e CPad::UpdatePads (0x541DD0).
//  - CTimer::m_FrameCounter = 0xB7CB4C. CPad do jogador (pad 0) = 0xB73458.
//    Bicicleta = veiculo+0x594 == 10 (BMX). Veiculo do jogador: FindPlayerVehicle 0x56E0D0.
//
// Enderecos do MENU (NAO testados neste exe: vem do plugin-sdk / gta-reversed; o mod confere tudo
// antes de alterar qualquer coisa e escreve o resultado no BikeTurbo.log quando Log = 1):
//  - FrontEndMenuManager = 0xBA6748. Campos: +0x54 item selecionado (int), +0x5C menu ativo (byte),
//    +0x15D pagina atual (char).
//  - aScreens = 0x8CE008, cada tela CMenuScreen tem 0xE2 bytes: nome[8], tela anterior, entrada pai,
//    e 12 entradas de 18 bytes (acao, chave de texto[8], slot, tela destino, pad, X, Y, alinhamento).
//  - CMenuManager::DrawStandardMenu = 0x5794A0. CText::Get (thiscall) = 0x6A0050: as chamadas a ela
//    dentro do desenho do menu sao trocadas por um filtro que devolve o texto da nossa opcao.
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static const uintptr_t VERSION_ADDR  = 0x82457C;
static const uint32_t  VERSION_OK    = 0x94BF;
static const uintptr_t FN_SETHELP    = 0x588BE0;
static const uintptr_t FN_DRAWHELP   = 0x58B6E0;
static const uintptr_t SITE_DRAWHELP = 0x58FCFA;
static const uintptr_t FN_UPDATEPADS = 0x541DD0;
static const uintptr_t SITE_UPDPADS  = 0x53BEE6;
static const uintptr_t PAD0          = 0xB73458;
static const int       OFF_CROSS     = 0x20;
static const uintptr_t FRAME_COUNTER = 0xB7CB4C;
static const uintptr_t FN_FINDVEH    = 0x56E0D0;
static const int       OFF_SUBCLASS  = 0x594;
static const int       SUBCLASS_BMX  = 10;

// ---- Turbo de socos (MeleeTurbo=1 no .ini) ----
// Confirmado no gta_sa.exe 1.0 US: FindPlayerPed = 0x56E210 (cdecl, -1 = jogador em foco); CPed::m_nActiveWeaponSlot = ped+0x718 (byte);
// CPed::m_aWeapons = ped+0x5A0, 0x1C bytes cada, tipo da arma no +0. Tipos 0..15 = punho e armas brancas.
// CPad::DisablePlayerControls = pad+0x10E (word).
static const uintptr_t FN_FINDPED       = 0x56E210;
static const int       OFF_ACTIVESLOT   = 0x718;
static const int       OFF_WEAPONS      = 0x5A0;
static const int       WEAPON_STRIDE    = 0x1C;
static const int       OFF_PADDISABLE   = 0x10E;
static const int       OFF_TRIANGLE     = 0x1E;     // Y / F: golpe alternativo (com a mira apertada)
static const int       OFF_R1           = 0x0C;     // RB / botao direito do mouse: mira
static volatile bool   gGym             = false;    // GymTurbo
static volatile bool   gMelee           = false;    // MeleeTurbo
static int             gMeleeFireOff    = 0x22;     // MeleeFireButton (padrao Circle = B / botao esquerdo do mouse)

// ---- Menu ----
static const uintptr_t FEM_ADDR      = 0xBA6748;   // FrontEndMenuManager
static const int       FEM_SELECTED  = 0x54;       // int  : entrada selecionada
static const int       FEM_ACTIVE    = 0x5C;       // byte : menu aberto
static const int       FEM_PAGE      = 0x15D;      // char : pagina atual
static const uintptr_t ASCREENS_ADDR = 0x8CE008;   // CMenuScreen aScreens[]
static const int       MAX_SCREENS   = 44;          // telas validas de aScreens (depois delas ha lixo)
static const uintptr_t FN_DRAWSTD    = 0x5794A0;   // CMenuManager::DrawStandardMenu
static const size_t    DRAWSTD_SCAN  = 0x3800;     // area varrida atras de chamadas a CText::Get
static const uintptr_t FN_TEXTGET    = 0x6A0050;   // CText::Get (thiscall)

#pragma pack(push, 2)
struct MenuEntry {
    uint8_t  action;
    char     name[8];
    uint8_t  slot;
    int8_t   target;
    uint8_t  pad;
    uint16_t x;
    uint16_t y;
    uint8_t  align;
    uint8_t  pad2;
};
struct MenuScreen {
    char      name[8];
    int8_t    prev;
    int8_t    parent;
    MenuEntry e[12];
};
#pragma pack(pop)
static_assert(sizeof(MenuEntry) == 18, "MenuEntry deve ter 18 bytes");
static_assert(sizeof(MenuScreen) == 0xE2, "MenuScreen deve ter 0xE2 bytes");

typedef void  (__cdecl   *SetHelp_t)(const char* text, int quick, int permanent, int brief);
typedef void* (__cdecl   *FindVeh_t)(int player, int remote);
typedef void  (__cdecl   *Void_t)();
typedef char* (__thiscall *TextGet_t)(void* self, const char* key);

static HMODULE hSelf = NULL;
static char    gIni[MAX_PATH];
static bool    gLog = false;

static char    gCode[24]    = "BIKETURBO";
static char    gTextOn[64]  = "Bike Turbo On";
static char    gTextOff[64] = "Bike Turbo Off";
static unsigned gHalfFrames = 1;       // frames "apertado" / frames "solto" em cada toque

static volatile bool gOn      = false;
static volatile int  gPending = 0;     // 0 nada, 1 mostrar "On", 2 mostrar "Off"

// ---- Menu: configuracao e estado ----
static char    gMenuText[96] = "Bike Turbo";
static char    gMenuYes[32]  = "Yes";
static char    gMenuNo[32]   = "No";
static char    gMenuFmt[96]  = "{text}: {value}";
static int     gMenuAction   = 254;            // acao da nova entrada (5 = a mesma de "Redefine Controls": o jogo sabe desenhar)
static const char gMenuKey[8] = "BKTURBO";   // chave de texto da nossa entrada (7 letras + NUL)
static volatile bool gMenuReady = false;
static int     gMenuStep     = 30;           // espaco automatico entre linhas (y=0 = "logo abaixo"): 72 px a 1080p
static int     gCtrlPage     = -1;           // indice da tela Controller Setup em aScreens
static const int FEM_NUMOPTS = 0x1B21;       // byte : quantidade de opcoes da pagina atual
static TextGet_t gOrigGet    = NULL;
static volatile int gTextAsks = 0;           // quantas vezes o jogo pediu o texto da nossa linha (ela foi desenhada)
static const int OFF_DPAD_LEFT = 0x14, OFF_DPAD_RIGHT = 0x16;   // CControllerState: DPadLeft / DPadRight

static void Log(const char* fmt, ...)
{
    if (!gLog) return;
    char path[MAX_PATH];
    if (!GetModuleFileNameA(hSelf, path, MAX_PATH)) return;
    char* dot = strrchr(path, '.');
    if (dot) strcpy(dot, ".log"); else strcat(path, ".log");
    FILE* f = fopen(path, "a");
    if (!f) return;
    fprintf(f, "[%lu] ", GetTickCount());
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fprintf(f, "\n");
    fclose(f);
}

static bool GameFocused()
{
    HWND h = GetForegroundWindow();
    DWORD pid = 0;
    if (h) GetWindowThreadProcessId(h, &pid);
    return pid == GetCurrentProcessId();
}

// ---------------- Estado salvo (BikeTurbo.sav ao lado do .asi: "1" ou "0") ----------------
static bool gSaveChoice = true;

static bool StatePath(char* path)
{
    if (!GetModuleFileNameA(hSelf, path, MAX_PATH)) return false;
    char* dot = strrchr(path, '.');
    if (dot) strcpy(dot, ".sav"); else strcat(path, ".sav");
    return true;
}

static bool LoadState(bool& on)
{
    char path[MAX_PATH];
    if (!StatePath(path)) return false;
    FILE* f = fopen(path, "r");
    if (!f) return false;
    int c = fgetc(f);
    fclose(f);
    if (c != '0' && c != '1') return false;
    on = (c == '1');
    return true;
}

// Muda o estado do turbo (menu, codigo digitado, Restore Defaults) e grava a escolha
static void SetOn(bool v)
{
    if (gOn == v) return;
    gOn = v;
    if (!gSaveChoice) return;
    char path[MAX_PATH];
    if (!StatePath(path)) return;
    FILE* f = fopen(path, "w");
    if (f) { fputc(v ? '1' : '0', f); fclose(f); }
}

// ---------------- INI proprio (aceita UTF-8 com ou sem BOM) ----------------
static char*  gIniData = NULL;
static size_t gIniLen  = 0;

static void IniLoad(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > (1 << 20)) { fclose(f); return; }
    gIniData = (char*)malloc((size_t)n + 1);
    if (!gIniData) { fclose(f); return; }
    gIniLen = fread(gIniData, 1, (size_t)n, f);
    fclose(f);
    gIniData[gIniLen] = 0;
    if (gIniLen >= 3 && (uint8_t)gIniData[0] == 0xEF && (uint8_t)gIniData[1] == 0xBB && (uint8_t)gIniData[2] == 0xBF)
    {
        memmove(gIniData, gIniData + 3, gIniLen - 2);   // inclui o NUL final
        gIniLen -= 3;
    }
}

typedef bool (*IniCb)(const char* sec, const char* key, const char* val, void* ctx);   // false = parar

static void IniEach(IniCb cb, void* ctx)
{
    if (!gIniData) return;
    char sec[64] = "";
    const char* p = gIniData;
    const char* end = gIniData + gIniLen;
    while (p < end)
    {
        const char* e = p;
        while (e < end && *e != '\n' && *e != '\r') ++e;
        size_t len = (size_t)(e - p);
        char line[512];
        if (len > sizeof(line) - 1) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = 0;
        p = e;
        while (p < end && (*p == '\n' || *p == '\r')) ++p;

        char* s = line;
        while (*s == ' ' || *s == '\t') ++s;
        if (!*s || *s == ';' || *s == '#') continue;
        if (*s == '[')
        {
            char* q = strchr(s, ']');
            if (q) { *q = 0; strncpy(sec, s + 1, sizeof(sec) - 1); sec[sizeof(sec) - 1] = 0; }
            continue;
        }
        char* eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char* kend = eq;
        while (kend > s && (kend[-1] == ' ' || kend[-1] == '\t')) --kend;
        *kend = 0;
        char* val = eq + 1;
        while (*val == ' ' || *val == '\t') ++val;
        char* vend = val + strlen(val);
        while (vend > val && (vend[-1] == ' ' || vend[-1] == '\t')) --vend;
        *vend = 0;
        if (!cb(sec, s, val, ctx)) return;
    }
}

struct IniGetCtx { const char* sec; const char* key; char* out; size_t cap; bool found; };
static bool IniGetCb(const char* sec, const char* key, const char* val, void* ctx)
{
    IniGetCtx* c = (IniGetCtx*)ctx;
    if (_stricmp(sec, c->sec) != 0 || _stricmp(key, c->key) != 0) return true;
    strncpy(c->out, val, c->cap - 1);
    c->out[c->cap - 1] = 0;
    c->found = true;
    return false;
}
static bool IniGet(const char* sec, const char* key, char* out, size_t cap)
{
    IniGetCtx c = { sec, key, out, cap, false };
    IniEach(IniGetCb, &c);
    return c.found;
}
static int IniInt(const char* key, int def)
{
    char b[32];
    if (!IniGet("BikeTurbo", key, b, sizeof b) || !b[0]) return def;
    return atoi(b);
}

// ---------------- Texto do .ini (UTF-8 ou ANSI) -> bytes que o jogo entende ----------------
struct CharMapEnt { uint32_t cp; uint8_t b; };
static CharMapEnt gMap[64];
static int        gMapN = 0;
static UINT       gCodePage = 1252;

static int Utf8Next(const uint8_t*& p, uint32_t& cp)   // 1 = ok, 0 = UTF-8 invalido
{
    uint8_t c = *p;
    if (c < 0x80) { cp = c; ++p; return 1; }
    int n; uint32_t v;
    if      ((c & 0xE0) == 0xC0) { n = 1; v = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { n = 2; v = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { n = 3; v = c & 0x07; }
    else return 0;
    for (int i = 1; i <= n; ++i)
    {
        if ((p[i] & 0xC0) != 0x80) return 0;
        v = (v << 6) | (p[i] & 0x3F);
    }
    p += n + 1;
    cp = v;
    return 1;
}

static void ToGame(const char* in, char* out, size_t cap)
{
    size_t k = 0;
    bool utf8 = true, hi = false;
    for (const uint8_t* p = (const uint8_t*)in; *p; )
    {
        uint32_t cp;
        if (*p >= 0x80) hi = true;
        if (!Utf8Next(p, cp)) { utf8 = false; break; }
    }
    if (!utf8 || !hi)                       // ASCII puro ou arquivo ANSI: bytes passam como estao
    {
        for (const char* s = in; *s && k + 1 < cap; ++s) out[k++] = *s;
        out[k] = 0;
        return;
    }
    for (const uint8_t* p = (const uint8_t*)in; *p && k + 1 < cap; )
    {
        uint32_t cp = 0;
        Utf8Next(p, cp);
        if (cp < 0x80) { out[k++] = (char)cp; continue; }
        bool done = false;
        for (int i = 0; i < gMapN; ++i)
            if (gMap[i].cp == cp) { out[k++] = (char)gMap[i].b; done = true; break; }
        if (done) continue;
        if (cp <= 0xFFFF)
        {
            WCHAR w = (WCHAR)cp;
            char b = '?';
            BOOL usedDef = FALSE;
            if (WideCharToMultiByte(gCodePage, 0, &w, 1, &b, 1, "?", &usedDef) == 1 && !usedDef) { out[k++] = b; continue; }
        }
        out[k++] = '?';
    }
    out[k] = 0;
}

static bool CharMapCb(const char* sec, const char* key, const char* val, void*)
{
    if (_stricmp(sec, "CharMap") != 0 || gMapN >= 64) return true;
    uint32_t cp = 0;
    if ((key[0] == 'U' || key[0] == 'u') && key[1] == '+') cp = (uint32_t)strtoul(key + 2, NULL, 16);
    else { const uint8_t* p = (const uint8_t*)key; if (!Utf8Next(p, cp)) return true; }
    long b = strtol(val, NULL, 0);
    if (cp < 0x80 || b < 1 || b > 255) return true;
    gMap[gMapN].cp = cp;
    gMap[gMapN].b = (uint8_t)b;
    ++gMapN;
    return true;
}

static void ReadText(const char* key, const char* def, char* out, size_t cap)
{
    char raw[256];
    if (!IniGet("BikeTurbo", key, raw, sizeof raw)) { strncpy(raw, def, sizeof raw - 1); raw[sizeof raw - 1] = 0; }
    ToGame(raw, out, cap);
}

// Monta o texto da nossa linha do menu (reflete o estado atual: Yes/No)
static void BuildLabel(char* out, size_t cap)
{
    const char* val = gOn ? gMenuYes : gMenuNo;
    const char* f = gMenuFmt;
    size_t k = 0;
    while (*f && k + 1 < cap)
    {
        if (strncmp(f, "{text}", 6) == 0)       { for (const char* s = gMenuText; *s && k + 1 < cap; ) out[k++] = *s++; f += 6; }
        else if (strncmp(f, "{value}", 7) == 0) { for (const char* s = val;       *s && k + 1 < cap; ) out[k++] = *s++; f += 7; }
        else out[k++] = *f++;
    }
    out[k] = 0;
}

// ---------------- Tabelas do menu e cliques (thread propria, mesma do codigo digitado) ----------------
// Outro mod pode manter uma COPIA das telas no heap (e o jogo desenha por ela, nao pela tabela do .exe).
// gLive = base dessa copia (indice = numero da tela). Sem copia, usa a tabela do .exe.
static MenuScreen* gLive = NULL;
static volatile int gKeepSel = -1;           // item a recolocar se o jogo recarregar a tela depois do clique
static volatile DWORD gKeepUntil = 0;

static MenuScreen* ActiveBase() { return gLive ? gLive : (MenuScreen*)ASCREENS_ADDR; }
static bool KeyIs(const MenuEntry& e, const char* key) { return strncmp(e.name, key, 8) == 0; }
static int CountEntries(const MenuScreen& s) { int n = 0; while (n < 12 && s.e[n].action != 0) ++n; return n; }

// Entrada que o jogo considera "item sel" da tela: compara a quantidade de entradas da tabela com a quantidade
// que o proprio jogo informa (algumas telas tem uma primeira linha de rotulo que nao conta como item).
static const MenuEntry* EntryAt(int page, int sel, int cnt)
{
    if (page < 0 || page >= MAX_SCREENS) return NULL;
    const MenuScreen& s = ActiveBase()[page];
    const int n = CountEntries(s);
    (void)cnt;                                // o log mostrou: o item N do jogo e a entrada N da tabela (o rotulo conta)
    const int i = sel;
    return (i >= 0 && i < n) ? &s.e[i] : NULL;
}

static void DumpScreen(int i, const MenuScreen& s);

// "Yes" da tela de confirmacao: a entrada FEM_YES e a ultima da tabela, entao e o ultimo item do jogo
// (funciona com ou sem a linha de rotulo da tela e mesmo que a numeracao dos itens seja diferente).
static bool IsYesItem(int conf, int sel, int /*cnt*/, const MenuEntry* e)
{
    if (e && KeyIs(*e, "FEM_YES")) return true;
    const MenuScreen& s = ActiveBase()[conf];
    const int n = CountEntries(s);
    return n > 0 && KeyIs(s.e[n - 1], "FEM_YES") && sel == n - 1;
}

// Trata um "clique": page/sel = onde o menu estava um instante ANTES do clique (o jogo pode ja ter reagido).
// arrow = seta esquerda/direita ou direcional: so alterna a nossa linha.
static void MenuClick(int page, int sel, int cnt, bool arrow)
{
    if (!gMenuReady || gCtrlPage < 0) return;
    const MenuEntry* e = EntryAt(page, sel, cnt);

    // tela de confirmacao do Restore Defaults = destino da linha FET_DEF da tela Controller Setup
    int conf = -1;
    const MenuScreen& cs = ActiveBase()[gCtrlPage];
    for (int k = 0; k < 12 && cs.e[k].action; ++k) if (KeyIs(cs.e[k], "FET_DEF")) conf = (int)(uint8_t)cs.e[k].target;

    if (page == gCtrlPage || (page == conf && conf >= 0))
    {
        Log("Menu: %s na pagina %d, item %d (opcoes no jogo: %d), chave \"%.8s\"", arrow ? "seta" : "clique", page, sel,
            cnt, e ? e->name : "(nenhuma)");
        if (page == conf && gLog) DumpScreen(conf, ActiveBase()[conf]);
    }

    if (page == gCtrlPage && e && KeyIs(*e, gMenuKey))
    {
        SetOn(!gOn);
        if (e->action == 5) { gKeepSel = sel; gKeepUntil = GetTickCount() + 400; }  // acao 5 recarrega a tela: recoloca o item
        Log("Menu: Bike Turbo -> %s", gOn ? "Yes" : "No");
    }
    else if (!arrow && conf >= 0 && page == conf && IsYesItem(conf, sel, cnt, e))
    {
        SetOn(false);                                                               // Restore Defaults confirmado: volta para No
        Log("Menu: Restore Defaults confirmado -> Bike Turbo = No");
    }
}

// ---------------- Roda do mouse: alterna a nossa linha (e nao deixa o jogo mover a selecao) ----------------
static LRESULT CALLBACK MouseLL(int code, WPARAM w, LPARAM l)
{
    if (code == HC_ACTION && w == WM_MOUSEWHEEL && gMenuReady && gCtrlPage >= 0 && GameFocused() &&
        *(volatile uint8_t*)(FEM_ADDR + FEM_ACTIVE))
    {
        const int page = *(volatile int8_t*)(FEM_ADDR + FEM_PAGE);
        const int sel  = *(volatile int*)(FEM_ADDR + FEM_SELECTED);
        const MenuEntry* e = EntryAt(page, sel, 0);
        if (page == gCtrlPage && e && KeyIs(*e, gMenuKey))
        {
            SetOn(!gOn);
            Log("Menu: roda do mouse -> Bike Turbo = %s", gOn ? "Yes" : "No");
            return 1;                                           // engole o evento
        }
    }
    return CallNextHookEx(NULL, code, w, l);
}

static DWORD WINAPI WheelThread(LPVOID)
{
    HHOOK h = SetWindowsHookExA(WH_MOUSE_LL, MouseLL, GetModuleHandleA(NULL), 0);
    if (!h) { Log("Menu: nao consegui instalar o gancho da roda do mouse (erro %lu).", GetLastError()); return 0; }
    MSG m;
    while (GetMessageA(&m, NULL, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageA(&m); }
    return 0;
}

static void DumpScreen(int i, const MenuScreen& s);

// Diagnostico: procura na memoria do processo o texto de uma chave (ex.: "FEC_MOU") e mostra os bytes em volta
static void ScanKey(const char* key)
{
    const size_t kl = strlen(key) + 1;                    // com o NUL final
    const uintptr_t selfLo = (uintptr_t)hSelf;
    int hits = 0;
    uintptr_t a = 0x10000;
    MEMORY_BASIC_INFORMATION mbi;
    while (a < 0x7FFE0000 && hits < 12 && VirtualQuery((void*)a, &mbi, sizeof mbi) == sizeof mbi)
    {
        uintptr_t lo = (uintptr_t)mbi.BaseAddress, hi = lo + mbi.RegionSize;
        const bool readable = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
                              (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                              PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
        if (readable && !(lo <= selfLo && selfLo < hi) && !(selfLo <= lo && lo < selfLo + 0x200000))
        {
            const uint8_t* p = (const uint8_t*)lo;
            for (size_t i = 0; i + kl <= mbi.RegionSize && hits < 12; ++i)
            {
                if (p[i] != (uint8_t)key[0] || memcmp(p + i, key, kl) != 0) continue;
                ++hits;
                char hex[200]; size_t o = 0;
                const uintptr_t at = lo + i;
                const uintptr_t from = (at >= lo + 20) ? at - 20 : lo;
                for (uintptr_t q = from; q < at + 40 && q < hi && o + 4 < sizeof hex; ++q)
                    o += (size_t)sprintf(hex + o, "%02X ", *(const uint8_t*)q);
                Log("Scan \"%s\" em 0x%08X (regiao 0x%08X-0x%08X, prot=0x%X). Bytes desde 0x%08X: %s",
                    key, (unsigned)at, (unsigned)lo, (unsigned)hi, (unsigned)mbi.Protect, (unsigned)from, hex);
            }
        }
        if (hi <= a) break;
        a = hi;
    }
    Log("Scan \"%s\": %d ocorrencia(s)", key, hits);
}

// Diagnostico (Log = 1): anota cada mudanca de pagina / item no menu
static void PatchTable(MenuScreen* S, int found);
static MenuScreen* FindLive();

// Garante que a copia do jogo (se existir) tambem tenha a nossa linha, e reaplica se alguem a refizer.
static void EnsureLive()
{
    static DWORD lastScan = 0;
    if (gCtrlPage < 0 || !*(volatile uint8_t*)(FEM_ADDR + FEM_ACTIVE)) return;
    if (gLive)
    {
        if (IsBadReadPtr(gLive + gCtrlPage, sizeof(MenuScreen)) || strncmp(gLive[gCtrlPage].name, "FET_CTL", 8) != 0)
        { Log("Menu: a copia das telas sumiu; procurando de novo."); gLive = NULL; return; }
        PatchTable(gLive, gCtrlPage);                     // so age se a linha nao estiver la
        return;
    }
    const DWORD now = GetTickCount();
    if (now - lastScan < 1000) return;
    lastScan = now;
    MenuScreen* L = FindLive();
    if (L)
    {
        Log("Menu: copia das telas do jogo encontrada em 0x%08X (tela %d em 0x%08X)", (unsigned)(uintptr_t)L, gCtrlPage, (unsigned)(uintptr_t)&L[gCtrlPage]);
        gLive = L;
        PatchTable(gLive, gCtrlPage);
    }
}

static void MenuWatch()
{
    static int lastPage = -999, lastSel = -999, lines = 0, dumps = 0;
    static DWORD enterTick = 0; static int asksAtEnter = 0; static bool fellBack = false;
    if (!gLog || lines > 300) return;
    if (!*(volatile uint8_t*)(FEM_ADDR + FEM_ACTIVE)) { lastPage = lastSel = -999; return; }
    const int page = *(volatile int8_t*)(FEM_ADDR + FEM_PAGE);
    const int sel  = *(volatile int*)(FEM_ADDR + FEM_SELECTED);
    if (gKeepSel >= 0)
    {
        if ((int)(GetTickCount() - gKeepUntil) > 0) gKeepSel = -1;
        else if (page == gCtrlPage && sel == 0 && gKeepSel != 0) { *(volatile int*)(FEM_ADDR + FEM_SELECTED) = gKeepSel; Log("Menu: item %d recolocado", (int)gKeepSel); gKeepSel = -1; return; }
    }
    if (page == gCtrlPage && page != lastPage) { enterTick = GetTickCount(); asksAtEnter = gTextAsks; }
    if (page == gCtrlPage && !fellBack && enterTick && GetTickCount() - enterTick > 1200 && gTextAsks == asksAtEnter)
    {
        // o jogo nao desenhou a linha com essa acao: usa a 5 (a de Redefine Controls), que ele sempre desenha
        fellBack = true;
        MenuScreen* tabs[2] = { (MenuScreen*)ASCREENS_ADDR, gLive };
        for (int t = 0; t < 2; ++t)
        {
            if (!tabs[t]) continue;
            MenuScreen& sc = tabs[t][gCtrlPage];
            for (int k = 0; k < 12 && sc.e[k].action; ++k)
                if (KeyIs(sc.e[k], gMenuKey) && sc.e[k].action != 5)
                {
                    DWORD old, tmp;
                    VirtualProtect(&sc.e[k], sizeof(MenuEntry), PAGE_READWRITE, &old);
                    sc.e[k].action = 5;
                    VirtualProtect(&sc.e[k], sizeof(MenuEntry), old, &tmp);
                    Log("Menu: a linha nao foi desenhada com a acao %d; trocada para a acao 5.", gMenuAction);
                }
        }
        gMenuAction = 5;
    }
    if (page == gCtrlPage && page != lastPage && dumps < 3)
    {
        ++dumps;
        Log("Menu: tabela da tela %d agora (para ver se o jogo a refez):", page);
        DumpScreen(page, ActiveBase()[page]);
    }
    if (page != lastPage || sel != lastSel)
    {
        lastPage = page; lastSel = sel; ++lines;
        Log("Menu: pagina=%d item=%d opcoes=%d", page, sel, (int)*(volatile uint8_t*)(FEM_ADDR + FEM_NUMOPTS));
    }
}

// ---------------- Deteccao do texto digitado (thread propria) ----------------
static DWORD WINAPI KeyThread(LPVOID)
{
    const int len = (int)strlen(gCode);
    char  buf[32] = { 0 };
    int   n = 0;
    bool  prev[26] = { false };
    bool  prevEnter = false, prevLmb = false, prevCross = false, prevL = false, prevR = false;
    DWORD last = 0;
    struct Snap { DWORD t; int page, sel, cnt; } hist[16] = {};
    int   histN = 0;

    for (;;)
    {
        Sleep(4);
        if (!GameFocused()) { for (int i = 0; i < 26; i++) prev[i] = false; prevEnter = prevLmb = prevCross = prevL = prevR = false; continue; }

        DWORD now = GetTickCount();
        if (n > 0 && now - last > 4000) n = 0;                // pausa longa: recomeca

        for (int i = 0; i < 26; i++)
        {
            bool down = (GetAsyncKeyState('A' + i) & 0x8000) != 0;
            if (down && !prev[i])
            {
                if (n == len) { memmove(buf, buf + 1, len - 1); n = len - 1; }
                buf[n++] = (char)('A' + i);
                buf[n] = 0;
                last = now;
                if (n == len && memcmp(buf, gCode, len) == 0)
                {
                    SetOn(!gOn);
                    gPending = gOn ? 1 : 2;
                    n = 0; buf[0] = 0;
                    Log("Turbo %s", gOn ? "LIGADO" : "DESLIGADO");
                }
            }
            prev[i] = down;
        }

        if (gMenuReady)
        {
            EnsureLive();
            MenuWatch();
            const bool active = *(volatile uint8_t*)(FEM_ADDR + FEM_ACTIVE) != 0;
            const int  page   = *(volatile int8_t*)(FEM_ADDR + FEM_PAGE);
            const int  sel    = *(volatile int*)(FEM_ADDR + FEM_SELECTED);
            const int cntNow = (int)*(volatile uint8_t*)(FEM_ADDR + FEM_NUMOPTS);
            hist[histN++ & 15] = { now, page, sel, cntNow };

            const bool enter = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
            const bool lmb   = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            const bool cross = *(volatile short*)(PAD0 + OFF_CROSS) > 0;                       // A / X do controle
            const bool left  = (GetAsyncKeyState(VK_LEFT)  & 0x8000) != 0 || *(volatile short*)(PAD0 + OFF_DPAD_LEFT)  > 0;
            const bool right = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0 || *(volatile short*)(PAD0 + OFF_DPAD_RIGHT) > 0;
            const bool press = (enter && !prevEnter) || (lmb && !prevLmb) || (cross && !prevCross);
            const bool arrow = (left && !prevL) || (right && !prevR);
            prevEnter = enter; prevLmb = lmb; prevCross = cross; prevL = left; prevR = right;

            if (active && (press || arrow))
            {
                // o jogo pode ja ter reagido a este toque: usa onde o menu estava ~30 ms antes
                int bp = page, bs = sel, bc = cntNow; bool got = false;
                for (int k = 1; k <= 15 && k < histN + 1; ++k)
                {
                    const Snap& h = hist[(histN - k) & 15];
                    if (h.t == 0) break;
                    bp = h.page; bs = h.sel; bc = h.cnt; got = true;
                    if (now - h.t >= 25) break;
                }
                if (!got) { bp = page; bs = sel; bc = cntNow; }
                MenuClick(bp, bs, bc, !press);
            }
        }
    }
    return 0;
}

// ---------------- Notificacao (na thread do jogo) ----------------
// Substitui "call CHud::DrawHelpMessage": mostra a mensagem pendente e desenha normalmente.
static void __cdecl HookDrawHelp()
{
    int p = gPending;
    if (p)
    {
        gPending = 0;
        ((SetHelp_t)FN_SETHELP)(p == 1 ? gTextOn : gTextOff, 1, 0, 0);   // mesma chamada do cheat
    }
    ((Void_t)FN_DRAWHELP)();
}

// ---------------- Turbo ----------------
static int gLogPulse = 0;

// Substitui "call CPad::UpdatePads" no inicio de CGame::Process
// Socos / armas brancas: com o turbo ligado, segurar o botao de ataque (B / botao do mouse) ou, mirando, o golpe alternativo
// (Y / F) repete o toque. So a pe, so com punho ou arma branca na mao, e so se os controles nao estiverem desativados.
// Retorna true se tratou este frame (o jogador esta em condicoes de socar).
static bool MeleeTurbo()
{
    if (*(volatile uint16_t*)(PAD0 + OFF_PADDISABLE) != 0) return false;           // cutscene / loja / controles desativados
    volatile short* fire = (volatile short*)(PAD0 + gMeleeFireOff);
    volatile short* tri  = (volatile short*)(PAD0 + OFF_TRIANGLE);
    volatile short* aim  = (volatile short*)(PAD0 + OFF_R1);
    const bool a = *fire > 0;                                  // ataque padrao
    const bool b = *tri > 0 && *aim > 0;                       // golpe alternativo: mira + Y / F
    if (!a && !b) return false;

    if (((FindVeh_t)FN_FINDVEH)(-1, 0)) return false;          // dentro de veiculo: nada
    uint8_t* ped = ((uint8_t* (__cdecl*)(int))FN_FINDPED)(-1);
    if (!ped) return false;
    const unsigned slot = ped[OFF_ACTIVESLOT];
    if (slot > 12) return false;
    const int type = *(volatile int*)(ped + OFF_WEAPONS + slot * WEAPON_STRIDE);
    if (type < 0 || type > 15) return false;                   // so punho (0) e armas brancas (1..15)

    static int logged = 0;
    const unsigned frame = *(volatile unsigned*)FRAME_COUNTER;
    if (((frame / gHalfFrames) & 1) != 0)                      // frames alternados: solta
    {
        if (a) *fire = 0;
        if (b) *tri = 0;
        if (logged < 6) { ++logged; Log("MeleeTurbo: arma %d, ataque=%d alternativo=%d (alternando o botao)", type, (int)a, (int)b); }
    }
    return true;
}


// ---------------- Academia (GymTurbo) ----------------
// O minigame da academia e feito por script; o jogo so toca animacoes do pacote "gymnasium" no jogador.
// Detecta isso olhando as animacoes ativas do jogador: se alguma pertence ao bloco "gymnasium", ele esta num aparelho.
//   esteira / bicicleta ergometrica (gym_tread_*, gym_bike_*): segurar A / Espaco repete o toque de Cross.
//   pesos: segurar A ou B (Espaco ou botao esquerdo) alterna Cross e Circle, como se voce apertasse A + B.
static const uintptr_t FN_GETANIMBLOCK = 0x4D3940;   // CAnimManager::GetAnimationBlock(name)
static const uintptr_t FN_FIRSTASSOC   = 0x4D15E0;   // RpAnimBlendClumpGetFirstAssociation(clump)
static const uintptr_t FN_UPKEY        = 0x53CF30;   // CKeyGen::GetUppercaseKey(str)
static const uintptr_t ADDR_ANIMS_PTR  = 0xB4EA34;   // CAnimManager::ms_aAnimations
static const int       OFF_PED_CLUMP   = 0x18;
static const int       OFF_ASSOC_NEXT  = 0x4;        // link.next (aponta para o link do proximo; associacao = link - 4)
static const int       OFF_ASSOC_GROUP = 0xE;
static const int       OFF_ASSOC_HIER  = 0x14;       // CAnimBlendHierarchy*
static const int       OFF_BLK_LOADED  = 0x10, OFF_BLK_FIRST = 0x14, OFF_BLK_COUNT = 0x18;

// animacoes de esteira e bicicleta (comparadas pelo hash do nome, maiusculas)
static const char* const kCardio[] = {
    "gym_bike_celebrate","gym_bike_fast","gym_bike_faster","gym_bike_getoff","gym_bike_geton","gym_bike_pedal","gym_bike_slow","gym_bike_still",
    "gym_tread_falloff","gym_tread_geton","gym_tread_getoff","gym_tread_jog","gym_tread_sprint","gym_tread_tired","gym_tread_walk" };
static unsigned gCardioHash[16]; static int gNCardio = 0;

// Procura, nos primeiros 0x30 bytes da associacao, um ponteiro para uma hierarquia do bloco "gymnasium"
// (tolera o deslocamento do campo estar errado). Devolve o ponteiro e o deslocamento achado.
static uint8_t* AssocGymHier(uint8_t* a, uintptr_t lo, uintptr_t hi, int* offFound)
{
    for (int off = 0; off < 0x30; off += 4)
    {
        uintptr_t v = *(uintptr_t*)(a + off);
        if (v >= lo && v < hi && ((v - lo) % 0x14) == 0) { if (offFound) *offFound = off; return (uint8_t*)v; }
    }
    return 0;
}

// 0 = fora de aparelho, 1 = pesos, 2 = esteira / bicicleta
static int GymMode(uint8_t* ped)
{
    uint8_t* blk = ((uint8_t* (__cdecl*)(const char*))FN_GETANIMBLOCK)("gymnasium");
    const bool loaded = blk && blk[OFF_BLK_LOADED];     // nos pesos o pacote pode ainda nao constar como carregado
    const unsigned first = loaded ? *(unsigned*)(blk + OFF_BLK_FIRST) : 0, cnt = loaded ? *(unsigned*)(blk + OFF_BLK_COUNT) : 0;
    uint8_t* hierBase = *(uint8_t**)ADDR_ANIMS_PTR;
    void* clump = *(void**)(ped + OFF_PED_CLUMP);
    if (!hierBase || !clump || cnt > 4096) return 0;
    // Medido no log: durante qualquer exercicio o jogo marca "controles desativados" com o bit 0x20 (valor 32)
    const bool gymCtl = (*(volatile uint16_t*)(PAD0 + OFF_PADDISABLE) & 0x20) != 0;

    const uintptr_t lo = (uintptr_t)hierBase + (uintptr_t)first * 0x14, hi = lo + (uintptr_t)cnt * 0x14;
    uint8_t* a = ((uint8_t* (__cdecl*)(void*))FN_FIRSTASSOC)(clump);
    int mode = 0;
    for (int n = 0; a && n < 64; ++n)
    {
        int off = -1;
        uint8_t* h = AssocGymHier(a, lo, hi, &off);
        // Medido no log: as animacoes tocadas pelo script da academia tem grupo = 0xFFFF (+0xE) e o pacote "gymnasium" carregado.
        const unsigned short group = *(unsigned short*)(a + OFF_ASSOC_GROUP);
        uint8_t* hp = *(uint8_t**)(a + OFF_ASSOC_HIER);
        if (h || (group == 0xFFFF && gymCtl))
        {
            if (!mode) mode = 1;
            if (!h) h = hp;
            unsigned hash = 0;
            if ((uintptr_t)h > 0x400000 && (uintptr_t)h < 0x7FFF0000) hash = *(unsigned*)h;
            for (int k = 0; k < gNCardio; ++k) if (gCardioHash[k] == hash) mode = 2;
            static int logged = 0;
            if (gLog && logged < 40) { ++logged; Log("GymTurbo: animacao de script durante exercicio: grupo=%04X hier=%p hash=%08X flags=%04X (modo %d)", (unsigned)group, h, hash, (unsigned)(*(unsigned*)(a + 0x2C) >> 16), mode); }
        }
        uint8_t* nx = *(uint8_t**)(a + OFF_ASSOC_NEXT);
        a = nx ? nx - 4 : 0;
    }
    return mode;
}

static bool GymTurbo()
{
    volatile short* cross  = (volatile short*)(PAD0 + OFF_CROSS);
    volatile short* circle = (volatile short*)(PAD0 + 0x22);
    const bool a = *cross > 0, b = *circle > 0;
    if (!a && !b) return false;
    if (((FindVeh_t)FN_FINDVEH)(-1, 0)) return false;
    uint8_t* ped = ((uint8_t* (__cdecl*)(int))FN_FINDPED)(-1);
    if (!ped) return false;
    const int mode = GymMode(ped);
    if (!mode) return false;

    const unsigned frame = *(volatile unsigned*)FRAME_COUNTER;
    const bool odd = ((frame / gHalfFrames) & 1) != 0;
    static int logged = 0;
    if (mode == 2)                                   // esteira / bicicleta: so Cross, so se A / Espaco estiver segurado
    {
        if (!a) return true;
        if (odd) *cross = 0;
    }
    else                                             // pesos: alterna Cross e Circle enquanto qualquer um estiver segurado
    {
        if (odd) { *cross = 0; *circle = 255; }
        else     { *cross = 255; *circle = 0; }
    }
    if (logged < 6) { ++logged; Log("GymTurbo: aparelho (modo %d: %s), alternando o botao", mode, mode == 2 ? "esteira/bicicleta" : "pesos"); }
    return true;
}


// Diagnostico do GymTurbo (so com Log=1): a cada ~30 frames registra, quando algo muda, o estado que a deteccao enxerga.
static void GymDiag()
{
    static int lines = 0; static unsigned nextF = 0; static unsigned long long lastSig = 0;
    const unsigned frame = *(volatile unsigned*)FRAME_COUNTER;
    if (lines >= 160 || frame < nextF) return;
    nextF = frame + 30;
    uint8_t* ped = ((uint8_t* (__cdecl*)(int))FN_FINDPED)(-1);
    if (!ped) return;
    uint8_t* blk = ((uint8_t* (__cdecl*)(const char*))FN_GETANIMBLOCK)("gymnasium");
    uint8_t* hierBase = *(uint8_t**)ADDR_ANIMS_PTR;
    void* clump = *(void**)(ped + OFF_PED_CLUMP);
    unsigned long long sig = 1469598103934665603ULL;
    char buf[1400]; int o = 0;
    o += _snprintf(buf + o, sizeof buf - o, "blk=%p loaded=%d first=%u cnt=%u clump=%p veh=%d dis=%u cross=%d circle=%d | assocs:",
        blk, blk ? (int)blk[OFF_BLK_LOADED] : -1, blk ? *(unsigned*)(blk + OFF_BLK_FIRST) : 0, blk ? *(unsigned*)(blk + OFF_BLK_COUNT) : 0,
        clump, ((FindVeh_t)FN_FINDVEH)(-1, 0) ? 1 : 0, (unsigned)*(volatile uint16_t*)(PAD0 + OFF_PADDISABLE),
        (int)*(volatile short*)(PAD0 + OFF_CROSS), (int)*(volatile short*)(PAD0 + 0x22));
    if (clump && hierBase)
    {
        const unsigned first = blk ? *(unsigned*)(blk + OFF_BLK_FIRST) : 0, cnt = blk ? *(unsigned*)(blk + OFF_BLK_COUNT) : 0;
        const uintptr_t lo = (uintptr_t)hierBase + (uintptr_t)first * 0x14, hi = lo + (uintptr_t)cnt * 0x14;
        o += _snprintf(buf + o, sizeof buf - o, " base=%p range=%08X-%08X", hierBase, (unsigned)lo, (unsigned)hi);
        uint8_t* a = ((uint8_t* (__cdecl*)(void*))FN_FIRSTASSOC)(clump);
        for (int n = 0; a && n < 8 && o < 1250; ++n)
        {
            int off = -1;
            uint8_t* h = AssocGymHier(a, lo, hi, &off);
            o += _snprintf(buf + o, sizeof buf - o, " {a=%p", a);
            for (int w = 0; w < 12; ++w) o += _snprintf(buf + o, sizeof buf - o, " %X", *(unsigned*)(a + w * 4));
            if (h) o += _snprintf(buf + o, sizeof buf - o, " GYM campo=+0x%X hash=%08X", off, *(unsigned*)h);
            { uint8_t* hp = *(uint8_t**)(a + OFF_ASSOC_HIER);
              if ((uintptr_t)hp > 0x400000 && (uintptr_t)hp < 0x7FFF0000)
                  o += _snprintf(buf + o, sizeof buf - o, " H=%X,%X,%X,%X,%X", *(unsigned*)hp, *(unsigned*)(hp+4), *(unsigned*)(hp+8), *(unsigned*)(hp+12), *(unsigned*)(hp+16)); }
            o += _snprintf(buf + o, sizeof buf - o, "}");
            sig = (sig ^ (unsigned long long)(uintptr_t)a) * 1099511628211ULL; sig = (sig ^ (h ? 1u : 0u)) * 1099511628211ULL;
            uint8_t* nx = *(uint8_t**)(a + OFF_ASSOC_NEXT);
            a = nx ? nx - 4 : 0;
        }
    }
    sig ^= (unsigned long long)(blk != 0) + ((unsigned long long)(blk && blk[OFF_BLK_LOADED]) << 1);
    if (sig == lastSig) return;
    lastSig = sig; ++lines;
    Log("GymDiag: %s", buf);
}

static void __cdecl HookUpdatePads()
{
    ((Void_t)FN_UPDATEPADS)();                     // o jogo le teclado/controle normalmente

    if (gGym && gLog) GymDiag();
    if (!gOn) return;
    if (*(volatile uint8_t*)(FEM_ADDR + FEM_ACTIVE)) return;      // menu / pausa aberto: nada de pulsar o botao (conflita com o A do menu)
    if (gGym && GymTurbo()) return;
    if (gMelee && MeleeTurbo()) return;
    volatile short* cross = (volatile short*)(PAD0 + OFF_CROSS);
    if (*cross <= 0) return;                        // nao esta segurando W / acelerar

    void* veh = ((FindVeh_t)FN_FINDVEH)(-1, 0);
    if (!veh) return;
    if (*(volatile int*)((uint8_t*)veh + OFF_SUBCLASS) != SUBCLASS_BMX) return;     // so bicicleta

    unsigned frame = *(volatile unsigned*)FRAME_COUNTER;
    if (((frame / gHalfFrames) & 1) != 0)           // frames alternados: solta o botao
    {
        *cross = 0;
        if (gLogPulse < 6) { ++gLogPulse; Log("Turbo ativo na bicicleta (alternando o botao de acelerar)"); }
    }
}

// ---------------- Menu: texto da nossa linha ----------------
// Substitui as chamadas a CText::Get dentro do desenho do menu: nossa chave devolve o texto atual.
static char* __fastcall HookTextGet(void* self, void* /*edx*/, const char* key)
{
    if (key && strncmp(key, gMenuKey, 8) == 0)      // "BKTURBO" + NUL
    {
        static char label[256];
        static int asked = 0;
        ++gTextAsks;
        BuildLabel(label, sizeof label);
        if (gLog && asked < 3) { ++asked; Log("Menu: o jogo pediu o texto da nossa linha -> \"%s\"", label); }
        return label;
    }
    if (gLog && key && *(volatile uint8_t*)(FEM_ADDR + FEM_ACTIVE) && *(volatile int8_t*)(FEM_ADDR + FEM_PAGE) == gCtrlPage)
    {
        static int seen = 0;
        if (seen < 24) { ++seen; Log("Menu: texto pedido na tela Controller Setup: \"%.8s\"", key); }
    }
    return gOrigGet(self, key);
}

// ---------------- Instalacao ----------------
static uintptr_t CallTarget(uintptr_t site)
{
    uint8_t* c = (uint8_t*)site;
    if (c[0] != 0xE8) return 0;
    return site + 5 + (uintptr_t)(intptr_t)(*(int32_t*)(c + 1));
}

static bool WriteCall(uintptr_t at, void* target)
{
    DWORD old;
    if (!VirtualProtect((void*)at, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    *(uint8_t*)at = 0xE8;
    *(int32_t*)(at + 1) = (int32_t)((uintptr_t)target - (at + 5));
    VirtualProtect((void*)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, 5);
    return true;
}

static void DumpScreen(int i, const MenuScreen& s)
{
    Log("  tela %d \"%.8s\" prev=%d pai=%d", i, s.name, (int)s.prev, (int)s.parent);
    for (int k = 0; k < 12 && (s.e[k].action || k < 5); ++k)
        Log("    [%d] acao=%u chave=\"%.8s\" slot=%u destino=%d x=%u y=%u alin=%u", k, s.e[k].action, s.e[k].name,
            s.e[k].slot, (int)s.e[k].target, s.e[k].x, s.e[k].y, s.e[k].align);
}

// Troca as chamadas a CText::Get dentro do desenho do menu pelo nosso filtro
static int PatchTextCalls()
{
    uintptr_t targets[48]; int counts[48]; int nt = 0;
    int patched = 0;
    const uint8_t* base = (const uint8_t*)FN_DRAWSTD;
    for (size_t i = 0; i + 5 <= DRAWSTD_SCAN; ++i)
    {
        if (base[i] != 0xE8) continue;
        uintptr_t site = FN_DRAWSTD + i;
        uintptr_t t = site + 5 + (uintptr_t)(intptr_t)(*(const int32_t*)(base + i + 1));
        if (t < 0x401000 || t > 0x860000) continue;           // nao e codigo do exe
        int j = 0;
        while (j < nt && targets[j] != t) ++j;
        if (j == nt && nt < 48) { targets[nt] = t; counts[nt] = 0; ++nt; }
        if (j < nt) ++counts[j];
        if (t == FN_TEXTGET && WriteCall(site, (void*)&HookTextGet)) { ++patched; i += 4; }
    }
    if (gLog)
    {
        Log("Chamadas mais frequentes em DrawStandardMenu (alvo: quantidade):");
        for (int r = 0; r < 6; ++r)
        {
            int best = -1;
            for (int j = 0; j < nt; ++j) if (counts[j] >= 0 && (best < 0 || counts[j] > counts[best])) best = j;
            if (best < 0) break;
            Log("    0x%X : %d", (unsigned)targets[best], counts[best]);
            counts[best] = -1;
        }
        Log("Chamadas a CText::Get (0x%X) trocadas: %d", (unsigned)FN_TEXTGET, patched);
    }
    return patched;
}

// Acha o indice da tela Controller Setup: nome "FET_CTL" e, nas linhas, FEC_RED (Redefine Controls) e
// FET_DEF (Restore Defaults) com UMA linha entre elas (Mouse Settings). Retorna -1 se nao for unico.
static bool IsCtrlScreen(const MenuScreen& s)
{
    if (strncmp(s.name, "FET_CTL", 8) != 0) return false;
    const int n = CountEntries(s);
    int r = -1, d = -1;
    for (int k = 0; k < n; ++k)
    {
        if (KeyIs(s.e[k], "FEC_RED")) r = k;
        if (KeyIs(s.e[k], "FET_DEF")) d = k;
    }
    return r >= 0 && d == r + 2 && n <= 11;
}

static int FindCtrlIndex()
{
    const MenuScreen* S = (const MenuScreen*)ASCREENS_ADDR;
    int found = -1, matches = 0;
    for (int i = 0; i < MAX_SCREENS; ++i)
        if (IsCtrlScreen(S[i])) { ++matches; found = i; Log("Menu: tela candidata %d", i); }
    if (matches != 1) { Log("Menu: %d telas candidatas (preciso de exatamente 1).", matches); return -1; }
    return found;
}

// Insere a nossa linha logo abaixo de Mouse Settings na tela S[found] (se ainda nao estiver la).
// Y: se o Mouse tem Y fixo, a nova linha fica um passo abaixo; se for 0 ("logo abaixo da anterior"), tambem 0.
// As linhas seguintes com Y fixo (Restore Defaults) descem um passo; as automaticas (Back) vao junto.
static void PatchTable(MenuScreen* S, int found)
{
    MenuScreen& s = S[found];
    const int n = CountEntries(s);
    for (int k = 0; k < n; ++k) if (KeyIs(s.e[k], gMenuKey)) return;      // ja inserida
    int r = -1, iDef = -1;
    for (int k = 0; k < n; ++k)
    {
        if (KeyIs(s.e[k], "FEC_RED")) r = k;
        if (KeyIs(s.e[k], "FET_DEF")) iDef = k;
    }
    if (r < 0 || iDef != r + 2 || n > 11) { Log("Menu: tabela em 0x%08X nao tem o formato esperado.", (unsigned)(uintptr_t)&s); return; }

    Log("Menu: tela %d em 0x%08X antes da alteracao:", found, (unsigned)(uintptr_t)&s);
    if (gLog) DumpScreen(found, s);

    DWORD old;
    if (!VirtualProtect(&s, sizeof(MenuScreen), PAGE_READWRITE, &old)) { Log("Menu: VirtualProtect falhou."); return; }

    MenuEntry nw = s.e[iDef - 1];                         // copia de "Mouse Settings": mesmo X, alinhamento
    memset(nw.name, 0, sizeof nw.name);
    memcpy(nw.name, gMenuKey, 7);
    nw.action = (uint8_t)gMenuAction;
    nw.slot = s.e[iDef].slot;                             // mesmo "slot" das linhas normais
    nw.target = (int8_t)found;                            // acao 5 = ir para a tela do destino: a mesma
    if (nw.y != 0) nw.y = (uint16_t)(nw.y + gMenuStep);

    for (int k = n; k > iDef; --k) s.e[k] = s.e[k - 1];   // Restore Defaults e Back descem uma linha
    s.e[iDef] = nw;
    for (int k = iDef + 1; k <= n; ++k)
        if (s.e[k].y != 0) s.e[k].y = (uint16_t)(s.e[k].y + gMenuStep);
    DWORD tmp;
    VirtualProtect(&s, sizeof(MenuScreen), old, &tmp);

    Log("Menu: linha inserida na tela %d (entrada %d). Novo layout:", found, iDef);
    if (gLog) DumpScreen(found, s);
}

// Procura na memoria privada (heap) uma copia gravavel da tela Controller Setup; devolve a base da copia
// (tela numero gCtrlPage esta em base[gCtrlPage]).
static MenuScreen* FindLive()
{
    static const char pat[8] = { 'F','E','T','_','C','T','L','\0' };
    uintptr_t a = 0x10000;
    MEMORY_BASIC_INFORMATION mbi;
    while (a < 0x7FFE0000 && VirtualQuery((void*)a, &mbi, sizeof mbi) == sizeof mbi)
    {
        const uintptr_t lo = (uintptr_t)mbi.BaseAddress, hi = lo + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_READWRITE &&
            mbi.RegionSize > sizeof(MenuScreen))
        {
            const uint8_t* p = (const uint8_t*)lo;
            const size_t lim = mbi.RegionSize - sizeof(MenuScreen);
            size_t i = 0;
            while (i <= lim)
            {
                const uint8_t* q = (const uint8_t*)memchr(p + i, 'F', lim - i + 1);
                if (!q) break;
                i = (size_t)(q - p);
                if (memcmp(q, pat, 8) == 0 && IsCtrlScreen(*(const MenuScreen*)q) &&
                    (uintptr_t)q >= lo + (uintptr_t)gCtrlPage * sizeof(MenuScreen))
                    return (MenuScreen*)(q - (uintptr_t)gCtrlPage * sizeof(MenuScreen));
                ++i;
            }
        }
        if (hi <= a) break;
        a = hi;
    }
    return NULL;
}

BOOL APIENTRY DllMain(HMODULE hm, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(hm);
    hSelf = hm;

    GetModuleFileNameA(hm, gIni, MAX_PATH);
    char* dot = strrchr(gIni, '.');
    if (dot) strcpy(dot, ".ini"); else strcat(gIni, ".ini");
    IniLoad(gIni);

    gLog = IniInt("Log", 0) != 0;
    if (!IniInt("Enable", 1)) { Log("Desativado no .ini"); return TRUE; }

    if (*(uint32_t*)VERSION_ADDR != VERSION_OK) { Log("Versao do exe nao reconhecida (esperado 1.0 US). Mod desativado."); return TRUE; }

    // Conversao de caracteres (UTF-8 -> bytes do jogo)
    int cpv = IniInt("CharCodePage", 1252);
    if (cpv > 0) gCodePage = (UINT)cpv;
    IniEach(CharMapCb, NULL);

    // Texto do codigo: so letras A-Z, 4 a 20 caracteres
    char code[32];
    ReadText("Code", "BIKETURBO", code, sizeof code);
    int k = 0;
    for (const char* p = code; *p && k < 20; ++p)
    {
        char c = *p; if (c >= 'a' && c <= 'z') c -= 32;
        if (c >= 'A' && c <= 'Z') gCode[k++] = c;
    }
    gCode[k] = 0;
    if (k < 4) strcpy(gCode, "BIKETURBO");

    ReadText("TextOn",  "Bike Turbo On",  gTextOn,  sizeof gTextOn);
    ReadText("TextOff", "Bike Turbo Off", gTextOff, sizeof gTextOff);
    ReadText("MenuText",   "Bike Turbo",      gMenuText, sizeof gMenuText);
    ReadText("MenuYes",    "Yes",             gMenuYes,  sizeof gMenuYes);
    ReadText("MenuNo",     "No",              gMenuNo,   sizeof gMenuNo);
    ReadText("MenuFormat", "{text}: {value}", gMenuFmt,  sizeof gMenuFmt);
    gMenuAction = IniInt("MenuAction", 254);
    if (gMenuAction < 1 || gMenuAction > 255) gMenuAction = 254;
    gMenuStep = IniInt("MenuRowStep", 30);
    if (gMenuStep < 1 || gMenuStep > 100) gMenuStep = 30;

    int frames = IniInt("TapFrames", 1);
    if (frames < 1) frames = 1;
    if (frames > 30) frames = 30;
    gHalfFrames = (unsigned)frames;
    gOn = IniInt("StartEnabled", 0) != 0;
    gSaveChoice = IniInt("SaveChoice", 1) != 0;
    gMelee = IniInt("MeleeTurbo", 0) != 0;
    gGym = IniInt("GymTurbo", 0) != 0;
    for (const char* nm : kCardio)
        if (gNCardio < 16) gCardioHash[gNCardio++] = ((unsigned (__cdecl*)(const char*))FN_UPKEY)(nm);
    {
        char fb[32];
        if (IniGet("BikeTurbo", "MeleeFireButton", fb, sizeof fb) && fb[0])
        {
            static const struct { const char* n; int off; } fbs[] = { {"Circle",0x22},{"Cross",0x20},{"Square",0x1C},{"Triangle",0x1E},
                                                                       {"R1",0x0C},{"L1",0x08},{"R2",0x0E},{"L2",0x0A} };
            for (const auto& e : fbs) if (_stricmp(fb, e.n) == 0) gMeleeFireOff = e.off;
        }
    }
    if (gSaveChoice) { bool sv; if (LoadState(sv)) gOn = sv; }       // a escolha salva vale mais que StartEnabled

    // Confere o codigo do jogo antes de alterar qualquer coisa
    bool ok = true;
    if (CallTarget(SITE_UPDPADS) != FN_UPDATEPADS) ok = false;
    if (CallTarget(SITE_DRAWHELP) != FN_DRAWHELP) ok = false;
    static const uint8_t setHelp[6] = { 0xA0, 0xC0, 0xAE, 0xBA, 0x00, 0x84 };
    if (memcmp((void*)FN_SETHELP, setHelp, sizeof setHelp) != 0) ok = false;
    if (!ok) { Log("Codigo do jogo diferente do esperado. Outro mod pode ter alterado essas areas. Mod desativado."); return TRUE; }

    bool a = WriteCall(SITE_UPDPADS, (void*)&HookUpdatePads);
    bool b = WriteCall(SITE_DRAWHELP, (void*)&HookDrawHelp);

    // Opcao no menu: so se os dois passos abaixo derem certo (primeiro o filtro de texto, depois a tabela)
    bool menu = false;
    if (!IniInt("HideMenuOption", 0))
    {
        gOrigGet = (TextGet_t)FN_TEXTGET;
        if (PatchTextCalls() > 0)
        {
            const int idx = FindCtrlIndex();
            if (idx >= 0) { gCtrlPage = idx; PatchTable((MenuScreen*)ASCREENS_ADDR, idx); menu = true; }
        }
        else Log("Menu: nenhuma chamada a CText::Get encontrada no desenho do menu. Opcao do menu desativada.");
        gMenuReady = menu;
    }

    CreateThread(NULL, 0, KeyThread, NULL, 0, NULL);
    if (menu && IniInt("MouseWheel", 1)) CreateThread(NULL, 0, WheelThread, NULL, 0, NULL);
    Log("GymTurbo=%d", (int)gGym);
    Log("MeleeTurbo=%d (botao de ataque no offset 0x%X)", (int)gMelee, gMeleeFireOff);
    Log("Ativo: codigo=%s, %u frame(s) por meio-toque, turbo inicial=%d, menu=%d, codepage=%u. Patch: pad=%s, mensagem=%s",
        gCode, gHalfFrames, (int)gOn, (int)menu, (unsigned)gCodePage, a ? "OK" : "FALHOU", b ? "OK" : "FALHOU");
    return TRUE;
}

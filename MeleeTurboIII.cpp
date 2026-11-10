// MeleeTurboIII.asi - GTA III PC v1.0
//
// Turbo de socos e do taco de beisebol: segurar o botao de ataque repete o golpe, sem precisar apertar varias vezes.
// So age a pe, com o punho ou o taco na mao, com o controle do personagem (nada de cutscenes, menus) e nunca com
// arma de fogo.
//
// Liga/desliga de tres jeitos (e a escolha e lembrada em MeleeTurboIII.sav):
//  - digitar MELEETURBO durante o jogo (buffer proprio: NAO usa o sistema de cheats do jogo, nao conta como cheat);
//  - opcao "Melee Turbo: No/Yes" no menu Controller Setup (abaixo de Mouse Settings; Restore Defaults volta para No);
//  - StartEnabled = 1 no .ini (estado inicial, se ainda nao houver escolha salva).
// A mensagem "Melee Turbo On / Off" aparece na caixa de aviso do jogo (a mesma dos cheats). Todos os textos sao
// editaveis no .ini, com suporte a caracteres especiais (UTF-8; veja [CharMap]).
// Compativel com o ClassicAxis (que regrava a tela Controller Setup): a linha e reinserida se a tela for refeita.
//
// Adaptado do MeleeTurboVC / BikeTurbo. Enderecos do gta3.exe 1.0 (conferidos no exe):
//  - CPad::UpdatePads chamado em 0x48C850; CPad 0 = 0x6F0360; DisablePlayerControls = pad+0xDF (byte).
//  - FindPlayerPed = 0x4A1150, FindPlayerVehicle = 0x4A10C0; ped+0x498 = slot da arma, ped+0x35C = armas (0x18 cada).
//  - CMenuManager = 0x8F59D8: +0x111 menu aberto (byte), +0x548 tela atual (int), +0x54C item selecionado (int).
//  - aScreens = 0x611930 (a tela 0 e vazia), telas de 0x184 bytes: nome[8], 5 ints, 18 entradas de 20 bytes
//    (acao, chave de texto[8], slot, destino). Controller Setup = tela 35 ("FET_CTL" com FET_RDK / FET_AMS / FET_DEF).
//  - CText::Get (thiscall, devolve wchar_t*) = 0x52C5A0; TheText = 0x941520. CHud::SetHelpMessage = 0x5051E0 (cdecl).
// O mod confere o codigo do jogo antes de alterar qualquer coisa e escreve o resultado no .log quando Log = 1.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstring>

static const uintptr_t VERSION_ADDR  = 0x5C1E70;     // conferencia do exe 1.0
static const uint32_t  VERSION_OK    = 0x53E58955;
static const uintptr_t SITE_UPDPADS  = 0x48C850;     // call CPad::UpdatePads (laco principal, uma vez por frame)
static const uintptr_t FN_UPDATEPADS = 0x492720;
static const uintptr_t FN_SETHELP    = 0x5051E0;     // CHud::SetHelpMessage(wchar_t*, quick)
static const uintptr_t PAD0          = 0x6F0360;
static const int       OFF_CROSS     = 0x20;
static const int       OFF_DPAD_LEFT = 0x14, OFF_DPAD_RIGHT = 0x16;   // CControllerState: DPadLeft / DPadRight
static const int       OFF_PADDISABLE = 0xDF;        // CPad::DisablePlayerControls (byte)
static const uintptr_t FN_FINDPED    = 0x4A1150;     // FindPlayerPed() (cdecl)
static const uintptr_t FN_FINDVEH    = 0x4A10C0;     // FindPlayerVehicle(): o veiculo, ou 0 a pe
static const int       OFF_CURSLOT   = 0x498;        // CPed: slot da arma atual (int8)
static const int       OFF_WEAPONS   = 0x35C;        // CPed: armas, 0x18 bytes cada, tipo da arma no +0
static const int       WEAPON_STRIDE = 0x18;
static const int       MELEE_MAX_TYPE = 1;           // 0 = punho, 1 = taco de beisebol
static int             gMeleeFireOff = 0x22;         // MeleeFireButton (padrao Circle)

// ---- Menu ----
static const uintptr_t FEM_ADDR      = 0x8F59D8;   // CMenuManager
static const int       FEM_SELECTED  = 0x54C;      // int  : entrada selecionada (= indice da entrada na tabela)
static const int       FEM_ACTIVE    = 0x111;      // byte : menu aberto
static const int       FEM_PAGE      = 0x548;      // int  : tela atual
static const uintptr_t ASCREENS_ADDR = 0x611930;   // CMenuScreen aScreens[] (tela 0 vazia)
static const int       MAX_SCREENS   = 57;         // telas validas de aScreens (0..56)
static const int       MAX_ENTRIES   = 18;
static const uintptr_t FN_TEXTGET    = 0x52C5A0;   // CText::Get (thiscall, devolve wchar_t*)

// Todas as chamadas a CText::Get de CMenuManager::DrawStandardMenu (titulo, rotulos e valores), conferidas no exe.
// Quem nao for a nossa chave passa direto para o CText::Get original.
static const uintptr_t TEXTGET_SITES[] = {
    0x47AF62, 0x47B072, 0x47B090, 0x47B0BA, 0x47B0D1, 0x47B0EB, 0x47B102, 0x47B120,
    0x47B61A, 0x47B667, 0x47B67F, 0x47B704, 0x47B71C, 0x47B73D, 0x47B75A, 0x47B77B,
    0x47B79A, 0x47B7BB, 0x47B7DA, 0x47B7FB, 0x47B81A, 0x47B8B6, 0x47B8CE, 0x47B8EF,
    0x47B90A, 0x47B92B, 0x47B94A, 0x47B96B, 0x47B98A, 0x47B9AB, 0x47B9CA, 0x47B9EB,
    0x47BA0A, 0x47BA2B, 0x47BA4A, 0x47BA6B, 0x47BA8A, 0x47BAAB, 0x47BACA, 0x47BB47,
    0x47BB5F, 0x47BB7E, 0x47BD47, 0x47BEB1, 0x47BF54, 0x47BFD4, 0x47C05B, 0x47C0C8,
    0x47C138, 0x47C18A, 0x47C263, 0x47C2BE, 0x47C306 };

struct MenuEntry {
    uint32_t action;
    char     name[8];
    uint32_t slot;
    int32_t  target;
};
struct MenuScreen {
    char      name[8];
    uint32_t  hdr[5];
    MenuEntry e[MAX_ENTRIES];
};
static_assert(sizeof(MenuEntry) == 20, "MenuEntry deve ter 20 bytes");
static_assert(sizeof(MenuScreen) == 0x184, "MenuScreen deve ter 0x184 bytes");

typedef void  (__cdecl   *SetHelp_t)(const wchar_t* text, int quick);
typedef void  (__cdecl   *Void_t)();
typedef void* (__cdecl   *PtrFn_t)();
typedef wchar_t* (__thiscall *TextGet_t)(void* self, const char* key);

static HMODULE hSelf = NULL;
static char    gIni[MAX_PATH];
static bool    gLog = false;

static char    gCode[24]    = "MELEETURBO";
static char    gTextOn[64]  = "Melee Turbo On";
static char    gTextOff[64] = "Melee Turbo Off";
static unsigned gHalfFrames = 1;       // frames "apertado" / frames "solto" em cada toque
static int     gSCodeSmall = 0x9A, gSCodeCap = 0x8A;      // codigos do jogo para s e S com caron

static volatile bool gOn      = false;
static volatile int  gPending = 0;     // 0 nada, 1 mostrar "On", 2 mostrar "Off"

// ---- Menu: configuracao e estado ----
static char    gMenuText[96] = "Melee Turbo";
static char    gMenuYes[32]  = "Yes";
static char    gMenuNo[32]   = "No";
static char    gMenuFmt[96]  = "{text}: {value}";
static int     gMenuAction   = 200;             // acao da nova entrada (200 = sem efeito; 2 = ir para tela, como "Mouse Settings")
static int     gFallbackAction = 2;            // acao usada se a linha nao for desenhada
static const char gMenuKey[8] = "MLTURBO";     // chave de texto da nossa entrada (7 letras + NUL)
static volatile bool gMenuReady = false;
static int     gCtrlPage     = -1;             // indice da tela Controller Setup em aScreens
static TextGet_t gOrigGet    = NULL;
static volatile int gTextAsks = 0;             // quantas vezes o jogo pediu o texto da nossa linha (ela foi desenhada)
static Void_t  gOrigUpdatePads = NULL;
static bool    gHideNothing  = false;

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

// ---------------- Estado salvo (MeleeTurboIII.sav ao lado do .asi: "1" ou "0") ----------------
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

// Muda o estado do turbo de socos (menu, codigo digitado, Restore Defaults) e grava a escolha
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
    if (!IniGet("MeleeTurbo", key, b, sizeof b) || !b[0]) return def;
    return atoi(b);
}

// ---------------- Texto do .ini (UTF-8 ou ANSI) -> bytes que o jogo entende ----------------
struct CharMapEnt { uint32_t cp; uint8_t b; };
static CharMapEnt gMap[64];
static int        gMapN = 0;

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

// Letras com acento viram a letra simples (a fonte do jogo nao tem a mesma ordem de glifos do San Andreas);
// s e S com caron (U+0161 / U+0160) usam os codigos SmallSCode / CapitalSCode; qualquer caractere pode ser
// trocado por um byte do jogo na secao [CharMap] do .ini.
static unsigned StripAccent(unsigned cp)
{
    static const char* from = "\xC0\xC1\xC2\xC3\xC4\xC5\xC7\xC8\xC9\xCA\xCB\xCC\xCD\xCE\xCF\xD1\xD2\xD3\xD4\xD5\xD6\xD9\xDA\xDB\xDC\xDD"
                              "\xE0\xE1\xE2\xE3\xE4\xE5\xE7\xE8\xE9\xEA\xEB\xEC\xED\xEE\xEF\xF1\xF2\xF3\xF4\xF5\xF6\xF9\xFA\xFB\xFC\xFD\xFF";
    static const char* to   = "AAAAAACEEEEIIIINOOOOOUUUUY"
                              "aaaaaaceeeeiiiinooooouuuuyy";
    if (cp >= 0xC0 && cp <= 0xFF)
    {
        const char* p = (const char*)memchr(from, (int)cp, strlen(from));
        if (p) return (unsigned char)to[p - from];
    }
    return '?';
}

static void ToGame(const char* in, char* out, size_t cap)
{
    size_t k = 0;
    const uint8_t* p = (const uint8_t*)in;
    while (*p && k + 1 < cap)
    {
        uint32_t cp = 0;
        if (*p < 0x80) { out[k++] = (char)*p++; continue; }
        const uint8_t* q = p;
        if (!Utf8Next(q, cp))                                  // byte solto (arquivo salvo como ANSI)
        {
            const unsigned c = *p++;
            out[k++] = (char)(c == 0x9A ? gSCodeSmall : c == 0x8A ? gSCodeCap : StripAccent(c));
            continue;
        }
        p = q;
        bool done = false;
        for (int i = 0; i < gMapN; ++i)
            if (gMap[i].cp == cp) { out[k++] = (char)gMap[i].b; done = true; break; }
        if (done) continue;
        out[k++] = (char)(cp == 0x161 ? gSCodeSmall : cp == 0x160 ? gSCodeCap : StripAccent(cp));
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
    if (!IniGet("MeleeTurbo", key, raw, sizeof raw)) { strncpy(raw, def, sizeof raw - 1); raw[sizeof raw - 1] = 0; }
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
static int CountEntries(const MenuScreen& s) { int n = 0; while (n < MAX_ENTRIES && s.e[n].action != 0) ++n; return n; }

// O item N do jogo e a entrada N da tabela.
static const MenuEntry* EntryAt(int page, int sel)
{
    if (page < 0 || page >= MAX_SCREENS) return NULL;
    const MenuScreen& s = ActiveBase()[page];
    const int n = CountEntries(s);
    return (sel >= 0 && sel < n) ? &s.e[sel] : NULL;
}

static void DumpScreen(int i, const MenuScreen& s);
static void PatchTable(MenuScreen* S, int found);
static MenuScreen* FindLive();
static bool IsCtrlScreen(const MenuScreen& s);

static inline int MenuPage() { return *(volatile int32_t*)(FEM_ADDR + FEM_PAGE); }
static inline int MenuSel()  { return *(volatile int32_t*)(FEM_ADDR + FEM_SELECTED); }
static inline bool MenuOpen() { return *(volatile uint8_t*)(FEM_ADDR + FEM_ACTIVE) != 0; }

// Trata um "clique": page/sel = onde o menu estava um instante ANTES do clique (o jogo pode ja ter reagido).
// arrow = seta esquerda/direita ou direcional: so alterna a nossa linha.
static void MenuClick(int page, int sel, bool arrow)
{
    if (!gMenuReady || gCtrlPage < 0 || page != gCtrlPage) return;
    const MenuEntry* e = EntryAt(page, sel);
    Log("Menu: %s na pagina %d, item %d, chave \"%.8s\"", arrow ? "seta" : "clique", page, sel, e ? e->name : "(nenhuma)");

    if (e && KeyIs(*e, gMenuKey))
    {
        SetOn(!gOn);
        if (e->action == (uint32_t)gFallbackAction) { gKeepSel = sel; gKeepUntil = GetTickCount() + 400; }  // acao "ir para tela" recarrega a tela: recoloca o item
        Log("Menu: Melee Turbo -> %s", gOn ? "Yes" : "No");
    }
    else if (!arrow && e && KeyIs(*e, "FET_DEF"))
    {
        SetOn(false);                                   // Restore Defaults: volta para No
        Log("Menu: Restore Defaults -> Melee Turbo = No");
    }
}

// ---------------- Roda do mouse: alterna a nossa linha (e nao deixa o jogo mover a selecao) ----------------
static LRESULT CALLBACK MouseLL(int code, WPARAM w, LPARAM l)
{
    if (code == HC_ACTION && w == WM_MOUSEWHEEL && gMenuReady && gCtrlPage >= 0 && GameFocused() && MenuOpen())
    {
        const MenuEntry* e = EntryAt(MenuPage(), MenuSel());
        if (MenuPage() == gCtrlPage && e && KeyIs(*e, gMenuKey))
        {
            SetOn(!gOn);
            Log("Menu: roda do mouse -> Melee Turbo = %s", gOn ? "Yes" : "No");
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

// Confere e reaplica na copia viva, com SEH (a memoria pode ter sido liberada pelo jogo). false = a copia sumiu.
static bool LivePatch()
{
    __try
    {
        if (strncmp(gLive[gCtrlPage].name, "FET_CTL", 8) != 0) return false;
        PatchTable(gLive, gCtrlPage);                     // so age se a linha nao estiver la
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Garante que a copia do jogo (se existir) tambem tenha a nossa linha, e reaplica se alguem a refizer.
static void EnsureLive()
{
    static DWORD lastScan = 0;
    if (gCtrlPage < 0) return;
    // Outro mod (ex.: ClassicAxis) pode regravar a tela inteira depois de nos: reaplica na tabela do exe
    {
        MenuScreen* ex = (MenuScreen*)ASCREENS_ADDR + gCtrlPage;
        if (IsCtrlScreen(*ex)) { bool has = false; for (int k = 0; k < MAX_ENTRIES; ++k) if (KeyIs(ex->e[k], gMenuKey)) has = true; if (!has) { Log("Menu: a tabela do exe foi refeita por outro mod; reaplicando."); PatchTable((MenuScreen*)ASCREENS_ADDR, gCtrlPage); } }
    }
    if (!MenuOpen()) return;
    if (gLive)
    {
        if (!LivePatch())
        { Log("Menu: a copia das telas sumiu; procurando de novo."); gLive = NULL; }
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
    if (!MenuOpen()) { lastPage = lastSel = -999; return; }
    const int page = MenuPage();
    const int sel  = MenuSel();

    // depois do clique o jogo recarrega a tela e leva a selecao para o inicio: recoloca no nosso item
    if (gKeepSel >= 0)
    {
        if ((int)(GetTickCount() - gKeepUntil) > 0) gKeepSel = -1;
        else if (page == gCtrlPage && sel <= 1 && gKeepSel > 1) { *(volatile int32_t*)(FEM_ADDR + FEM_SELECTED) = gKeepSel; Log("Menu: item %d recolocado", (int)gKeepSel); gKeepSel = -1; return; }
    }
    // se o jogo nao desenhou a linha com a acao escolhida, usa a de "Mouse Settings", que ele sempre desenha
    if (page == gCtrlPage && page != lastPage) { enterTick = GetTickCount(); asksAtEnter = gTextAsks; }
    if (page == gCtrlPage && !fellBack && gMenuAction != gFallbackAction && enterTick && GetTickCount() - enterTick > 1200 && gTextAsks == asksAtEnter)
    {
        fellBack = true;
        MenuScreen* tabs[2] = { (MenuScreen*)ASCREENS_ADDR, gLive };
        for (int t = 0; t < 2; ++t)
        {
            if (!tabs[t]) continue;
            MenuScreen& sc = tabs[t][gCtrlPage];
            for (int k = 0; k < MAX_ENTRIES && sc.e[k].action; ++k)
                if (KeyIs(sc.e[k], gMenuKey) && sc.e[k].action != (uint32_t)gFallbackAction)
                {
                    DWORD old, tmp;
                    VirtualProtect(&sc.e[k], sizeof(MenuEntry), PAGE_READWRITE, &old);
                    sc.e[k].action = (uint32_t)gFallbackAction; sc.e[k].target = gCtrlPage;
                    VirtualProtect(&sc.e[k], sizeof(MenuEntry), old, &tmp);
                    Log("Menu: a linha nao foi desenhada com a acao %d; trocada para a acao %d.", gMenuAction, gFallbackAction);
                }
        }
        gMenuAction = gFallbackAction;
    }
    if (gLog && lines <= 300)
    {
        if (page == gCtrlPage && page != lastPage && dumps < 3)
        {
            ++dumps;
            Log("Menu: tabela da tela %d agora (para ver se o jogo a refez):", page);
            DumpScreen(page, ActiveBase()[page]);
        }
        if (page != lastPage || sel != lastSel) { ++lines; Log("Menu: pagina=%d item=%d", page, sel); }
    }
    lastPage = page; lastSel = sel;
}

// ---------------- Deteccao do texto digitado + cliques no menu (thread propria) ----------------
static DWORD WINAPI KeyThread(LPVOID)
{
    const int len = (int)strlen(gCode);
    char  buf[32] = { 0 };
    int   n = 0;
    bool  prev[26] = { false };
    bool  prevEnter = false, prevLmb = false, prevCross = false, prevL = false, prevR = false;
    DWORD last = 0;
    struct Snap { DWORD t; int page, sel; } hist[16] = {};
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
                    Log("Melee Turbo %s", gOn ? "LIGADO" : "DESLIGADO");
                }
            }
            prev[i] = down;
        }

        if (gMenuReady)
        {
            EnsureLive();
            MenuWatch();
            const bool active = MenuOpen();
            const int  page   = MenuPage();
            const int  sel    = MenuSel();
            hist[histN++ & 15] = { now, page, sel };

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
                int bp = page, bs = sel; bool got = false;
                for (int k = 1; k <= 15 && k < histN + 1; ++k)
                {
                    const Snap& h = hist[(histN - k) & 15];
                    if (h.t == 0) break;
                    bp = h.page; bs = h.sel; got = true;
                    if (now - h.t >= 25) break;
                }
                if (!got) { bp = page; bs = sel; }
                MenuClick(bp, bs, !press);
            }
        }
    }
    return 0;
}

// ---------------- Turbo de socos (roda na thread do jogo, logo depois da leitura do controle) ----------------
static bool MeleeTurbo()
{
    if (*(volatile uint8_t*)(PAD0 + OFF_PADDISABLE) != 0) return false;     // cutscene / loja / script tomou o controle
    volatile int16_t* fire = (volatile int16_t*)(PAD0 + gMeleeFireOff);
    if (*fire <= 0) return false;                                            // botao de ataque nao esta apertado
    if (((PtrFn_t)FN_FINDVEH)()) return false;                               // dentro de veiculo
    uint8_t* ped = (uint8_t*)((PtrFn_t)FN_FINDPED)();
    if ((uintptr_t)ped < 0x10000) return false;
    const int slot = (int8_t)ped[OFF_CURSLOT];
    if (slot < 0 || slot > 12) return false;
    const int type = *(volatile int32_t*)(ped + OFF_WEAPONS + (unsigned)slot * WEAPON_STRIDE);
    if (type < 0 || type > MELEE_MAX_TYPE) return false;                     // so punho e taco
    static unsigned frame = 0; ++frame;
    if (((frame / gHalfFrames) & 1) != 0)                                    // frames alternados: solta o botao
    {
        *fire = 0;
        static int logged = 0;
        if (logged < 6) { ++logged; Log("Turbo: arma %d, alternando o botao de ataque", type); }
    }
    return true;
}

static void __cdecl HookUpdatePads()
{
    gOrigUpdatePads();                                   // o jogo le teclado / controle normalmente (e outros mods encadeados)

    const int p = gPending;                              // "Melee Turbo On / Off": mesma chamada dos cheats
    if (p)
    {
        gPending = 0;
        const char* t = (p == 1) ? gTextOn : gTextOff;
        wchar_t w[96]; size_t i = 0;
        for (; t[i] && i + 1 < 96; ++i) w[i] = (wchar_t)(unsigned char)t[i];
        w[i] = 0;
        ((SetHelp_t)FN_SETHELP)(w, 1);
    }
    if (!gOn) return;
    if (MenuOpen()) return;                              // menu / pausa aberto: nada de pulsar o botao
    MeleeTurbo();
}

// ---------------- Menu: texto da nossa linha ----------------
// Substitui as chamadas a CText::Get dentro do desenho do menu: nossa chave devolve o texto atual.
static wchar_t* __fastcall HookTextGet(void* self, void* /*edx*/, const char* key)
{
    if (key && strncmp(key, gMenuKey, 8) == 0)      // "MLTURBO" + NUL
    {
        static wchar_t label[256];
        static int asked = 0;
        char narrow[256];
        ++gTextAsks;
        BuildLabel(narrow, sizeof narrow);
        size_t i = 0;
        for (; narrow[i] && i + 1 < 256; ++i) label[i] = (wchar_t)(unsigned char)narrow[i];
        label[i] = 0;
        if (gLog && asked < 3) { ++asked; Log("Menu: o jogo pediu o texto da nossa linha -> \"%s\"", narrow); }
        return label;
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
    Log("  tela %d \"%.8s\" hdr=%u,%u,%u,%u,%u", i, s.name, s.hdr[0], s.hdr[1], s.hdr[2], s.hdr[3], s.hdr[4]);
    for (int k = 0; k < MAX_ENTRIES && (s.e[k].action || k < 5); ++k)
        Log("    [%d] acao=%u chave=\"%.8s\" slot=%u destino=%d", k, (unsigned)s.e[k].action, s.e[k].name, (unsigned)s.e[k].slot, (int)s.e[k].target);
}

// Troca as chamadas a CText::Get dentro do desenho do menu pelo nosso filtro
static int PatchTextCalls()
{
    int patched = 0;
    for (uintptr_t site : TEXTGET_SITES)
    {
        const uintptr_t t = CallTarget(site);
        if (t != FN_TEXTGET) { Log("Menu: a chamada em 0x%X nao aponta para CText::Get (aponta para 0x%X); ignorada.", (unsigned)site, (unsigned)t); continue; }
        if (WriteCall(site, (void*)&HookTextGet)) ++patched;
    }
    Log("Chamadas a CText::Get (0x%X) trocadas: %d de %d", (unsigned)FN_TEXTGET, patched, (int)(sizeof(TEXTGET_SITES) / sizeof(TEXTGET_SITES[0])));
    return patched;
}

// Acha o indice da tela Controller Setup: nome "FET_CTL" e, nas linhas, FET_RDK (Redefine Controls) e
// FET_DEF (Restore Defaults) com UMA linha entre elas (FET_AMS = Mouse Settings).
static bool IsCtrlScreen(const MenuScreen& s)
{
    if (strncmp(s.name, "FET_CTL", 8) != 0) return false;
    const int n = CountEntries(s);
    int r = -1, d = -1;
    for (int k = 0; k < n; ++k)
    {
        if (KeyIs(s.e[k], "FET_RDK")) r = k;
        if (KeyIs(s.e[k], "FET_DEF")) d = k;
    }
    return r >= 0 && d == r + 2 && n <= MAX_ENTRIES - 1;
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
// Restore Defaults e Back descem uma linha. No GTA III as linhas nao tem posicao propria: o jogo as distribui
// pelo indice, entao o espacamento e o mesmo do menu original.
static void PatchTable(MenuScreen* S, int found)
{
    MenuScreen& s = S[found];
    const int n = CountEntries(s);
    for (int k = 0; k < n; ++k) if (KeyIs(s.e[k], gMenuKey)) return;      // ja inserida
    int r = -1, iDef = -1;
    for (int k = 0; k < n; ++k)
    {
        if (KeyIs(s.e[k], "FET_RDK")) r = k;
        if (KeyIs(s.e[k], "FET_DEF")) iDef = k;
    }
    if (r < 0 || iDef != r + 2 || n > MAX_ENTRIES - 1) { Log("Menu: tabela em 0x%08X nao tem o formato esperado.", (unsigned)(uintptr_t)&s); return; }

    Log("Menu: tela %d em 0x%08X antes da alteracao:", found, (unsigned)(uintptr_t)&s);
    if (gLog) DumpScreen(found, s);

    DWORD old;
    if (!VirtualProtect(&s, sizeof(MenuScreen), PAGE_READWRITE, &old)) { Log("Menu: VirtualProtect falhou."); return; }

    MenuEntry nw = s.e[iDef - 1];                         // copia de "Mouse Settings"
    gFallbackAction = (int)nw.action;                     // a acao do Mouse Settings: o jogo sempre sabe desenhar
    memset(nw.name, 0, sizeof nw.name);
    memcpy(nw.name, gMenuKey, 7);
    nw.action = (uint32_t)gMenuAction;
    nw.slot = s.e[iDef].slot;
    nw.target = (gMenuAction == gFallbackAction) ? found : 0;   // "ir para tela" = a mesma tela (recarrega); outra = sem destino

    for (int k = n; k > iDef; --k) s.e[k] = s.e[k - 1];   // Restore Defaults e Back descem uma linha
    s.e[iDef] = nw;
    DWORD tmp;
    VirtualProtect(&s, sizeof(MenuScreen), old, &tmp);

    Log("Menu: linha inserida na tela %d (entrada %d). Novo layout:", found, iDef);
    if (gLog) DumpScreen(found, s);
}

// Varre UMA regiao. Protegida por SEH: o jogo pode liberar/trocar essa memoria enquanto lemos (ex.: ao minimizar e voltar,
// quando o D3D recria recursos), e a leitura de memoria que sumiu derrubava o jogo.
static MenuScreen* ScanRegion(uintptr_t lo, size_t size)
{
    static const char pat[8] = { 'F','E','T','_','C','T','L','\0' };
    MenuScreen* result = NULL;
    __try
    {
        const uint8_t* p = (const uint8_t*)lo;
        const size_t lim = size - sizeof(MenuScreen);
        size_t i = 0;
        while (i <= lim)
        {
            const uint8_t* q = (const uint8_t*)memchr(p + i, 'F', lim - i + 1);
            if (!q) break;
            i = (size_t)(q - p);
            if (memcmp(q, pat, 8) == 0 && IsCtrlScreen(*(const MenuScreen*)q) &&
                (uintptr_t)q >= lo + (uintptr_t)gCtrlPage * sizeof(MenuScreen))
            { result = (MenuScreen*)(q - (uintptr_t)gCtrlPage * sizeof(MenuScreen)); break; }
            ++i;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { result = NULL; }
    return result;
}

// Procura na memoria privada (heap) uma copia gravavel da tela Controller Setup (tela gCtrlPage = base[gCtrlPage]).
static MenuScreen* FindLive()
{
    uintptr_t a = 0x10000;
    MEMORY_BASIC_INFORMATION mbi;
    while (a < 0x7FFE0000 && VirtualQuery((void*)a, &mbi, sizeof mbi) == sizeof mbi)
    {
        const uintptr_t lo = (uintptr_t)mbi.BaseAddress, hi = lo + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_READWRITE &&
            mbi.RegionSize > sizeof(MenuScreen))
        {
            MenuScreen* r = ScanRegion(lo, mbi.RegionSize);
            if (r) return r;
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

    if (*(uint32_t*)VERSION_ADDR != VERSION_OK) { Log("Versao do exe nao reconhecida (esperado GTA III 1.0). Mod desativado."); return TRUE; }

    // Conversao de caracteres (UTF-8 -> codigos do jogo)
    IniEach(CharMapCb, NULL);
    { char b[32];
      if (IniGet("MeleeTurbo", "SmallSCode",   b, sizeof b) && b[0]) gSCodeSmall = (int)strtol(b, NULL, 0);
      if (IniGet("MeleeTurbo", "CapitalSCode", b, sizeof b) && b[0]) gSCodeCap   = (int)strtol(b, NULL, 0); }

    // Texto do codigo: so letras A-Z, 4 a 20 caracteres
    char code[32];
    ReadText("Code", "MELEETURBO", code, sizeof code);
    int k = 0;
    for (const char* p = code; *p && k < 20; ++p)
    {
        char c = *p; if (c >= 'a' && c <= 'z') c -= 32;
        if (c >= 'A' && c <= 'Z') gCode[k++] = c;
    }
    gCode[k] = 0;
    if (k < 4) strcpy(gCode, "MELEETURBO");

    ReadText("TextOn",  "Melee Turbo On",  gTextOn,  sizeof gTextOn);
    ReadText("TextOff", "Melee Turbo Off", gTextOff, sizeof gTextOff);
    ReadText("MenuText",   "Melee Turbo",     gMenuText, sizeof gMenuText);
    ReadText("MenuYes",    "Yes",             gMenuYes,  sizeof gMenuYes);
    ReadText("MenuNo",     "No",              gMenuNo,   sizeof gMenuNo);
    ReadText("MenuFormat", "{text}: {value}", gMenuFmt,  sizeof gMenuFmt);
    gMenuAction = IniInt("MenuAction", 200);
    if (gMenuAction < 1 || gMenuAction > 255) gMenuAction = 200;

    int frames = IniInt("TapFrames", 1);
    if (frames < 1) frames = 1;
    if (frames > 30) frames = 30;
    gHalfFrames = (unsigned)frames;
    gOn = IniInt("StartEnabled", 0) != 0;
    gSaveChoice = IniInt("SaveChoice", 1) != 0;
    {
        char fb[32];
        if (IniGet("MeleeTurbo", "MeleeFireButton", fb, sizeof fb) && fb[0])
        {
            static const struct { const char* n; int off; } fbs[] = { {"Circle",0x22},{"Cross",0x20},{"Square",0x1C},{"Triangle",0x1E},
                                                                       {"R1",0x0C},{"L1",0x08},{"R2",0x0E},{"L2",0x0A} };
            for (const auto& e : fbs) if (_stricmp(fb, e.n) == 0) gMeleeFireOff = e.off;
        }
    }
    if (gSaveChoice) { bool sv; if (LoadState(sv)) gOn = sv; }       // a escolha salva vale mais que StartEnabled

    // Confere o codigo do jogo antes de alterar qualquer coisa. O gancho do UpdatePads entra na cadeia se outro mod
    // (ex.: RadarZoomIII) ja trocou a chamada.
    const uintptr_t updTarget = CallTarget(SITE_UPDPADS);
    if (!updTarget) { Log("Codigo do jogo diferente do esperado (UpdatePads). Mod desativado."); return TRUE; }
    if (updTarget != FN_UPDATEPADS) Log("UpdatePads: o destino e 0x%X (esperado 0x%X): outro mod ja trocou a chamada; entrando na cadeia.", (unsigned)updTarget, (unsigned)FN_UPDATEPADS);
    static const uint8_t setHelp[7] = { 0x80, 0x3D, 0x5B, 0xCD, 0x95, 0x00, 0x01 };
    if (memcmp((void*)FN_SETHELP, setHelp, sizeof setHelp) != 0) { Log("Codigo do jogo diferente do esperado (SetHelpMessage). Mod desativado."); return TRUE; }

    gOrigUpdatePads = (Void_t)updTarget;
    const bool a = WriteCall(SITE_UPDPADS, (void*)&HookUpdatePads);

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
        else Log("Menu: nenhuma chamada a CText::Get trocada no desenho do menu. Opcao do menu desativada.");
        gMenuReady = menu;
    }

    CreateThread(NULL, 0, KeyThread, NULL, 0, NULL);
    if (menu && IniInt("MouseWheel", 1)) CreateThread(NULL, 0, WheelThread, NULL, 0, NULL);
    Log("Ativo: codigo=%s, %u frame(s) por meio-toque, turbo inicial=%d, menu=%d, botao de ataque 0x%X. Patch: pad=%s",
        gCode, gHalfFrames, (int)gOn, (int)menu, gMeleeFireOff, a ? "OK" : "FALHOU");
    return TRUE;
}

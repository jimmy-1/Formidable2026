#include <stdio.h>
#pragma pack(push,1)
struct CommandPkg { unsigned cmd; unsigned arg1; unsigned arg2; char data[1]; };
struct ClientInfo { char a[64]; char b[64]; char c[64]; char d[128]; char e[64]; char f[256]; char g[64]; char h[64]; char i[32]; unsigned j; int k; int l; int m; int n; int o; wchar_t p[256]; wchar_t q[128]; };
#pragma pack(pop)
int main() { printf(\
CommandPkg=%d
ClientInfo=%d\n\, sizeof(struct CommandPkg), sizeof(struct ClientInfo)); return 0; }

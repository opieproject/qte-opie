#include "stdio.h"
#include <qstring.h>

#define ISTASHKEEL(x) ((x >= 0x64B && x<=0x658) || (x>=0x6d6 && x <= 0x6dc) || (x>=0x6df && x <= 0x6e4) || x==0x6e7 || x == 0x6e8 || (x>=0x6ea && x <= 0x6ed) || (x>=0xfe70 && x <= 0xfe7f))

void qApplyBidi(const QString& s, QString& news);

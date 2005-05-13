#include <malloc.h>
#include "qbidi.h"


#define BLOCKTYPE unsigned short*
#define CHARTYPE unsigned short

extern "C" {

int doShape(BLOCKTYPE line, CHARTYPE* to, int from, int count);
int doBidi(BLOCKTYPE line, int count, int applyShape, int reorderCombining, int removeMarks);

}

void qApplyBidi(const QString& s, QString& news) {
	//convert to utf16 zero-terminated
	//printf(": qs length is %d\n",s.length());
        int loopc;
	int slength = sizeof(unsigned short) * (s.length());
	//printf(": slength is %d\n",slength);
	unsigned short* sutf16 = (unsigned short*)malloc(slength);
	for( loopc=0; loopc < int(s.length()); loopc++ ) {
	  sutf16[loopc] = s[loopc].unicode();
	  //printf(": char %d is %x\n",loopc,sutf16[loopc]);
	}
	//printf(": mark 0\n");
	///sutf16[s.length()] = 0;

	unsigned short* sutf16s = (unsigned short*)malloc(slength);
	doShape(sutf16,sutf16s,0,s.length());
	//printf(": do bidi\n");
	doBidi(sutf16s, s.length(),0,0,0);
	//sutf16s[s.length()] = 0;

	//printf(": back to QString\n");
	news = "";
	for( loopc=0; loopc < s.length(); loopc++ ) {
	  QChar newChar((short) sutf16s[loopc]);
	  news = news + newChar;
	  //printf(": add char %x\n",newChar.unicode());
	}

	free(sutf16);
	free(sutf16s);
}

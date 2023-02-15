#include"Test.h"
#include"XMap.h"
#include"XEquality.h"
void XMapTest()
{
	XMap* map = XMap_init(sizeof(int),sizeof(char*),XEquality_int_XMap, XEquality_int_XMap);

}
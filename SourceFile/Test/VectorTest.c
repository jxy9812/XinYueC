#include"Test.h"
#include"XVector.h"
struct people
{
	int age;
	char gender[10];
	char name[20];
	char achievement[20];
};
void VectorTest()
{
	XVector* v = XVector_init(" people ", sizeof(struct people));
	struct people p1 = { 22, "男", "琦神","大佬" };
	XVector_Push_Back(v, &p1);
	struct people p2 = { 19, "男", "小白","大佬" };
	XVector_Push_Back(v, &p2);
	int n = v->size(v);
	for (size_t i = 0; i < v->size(v); i++)
	{
		struct people* p = XVector_at(v, i);
		printf("%s %s %d岁 是%s\n", p->name, p->gender, p->age, p->achievement);
	}
	v->free(v);
}
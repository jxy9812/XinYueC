#ifndef  XVECTOR_ITERATOR_H
#define XVECTOR_ITERATOR_H
struct XVector;
//正向迭代器
typedef struct XVector_iterator
{
	char null;
}XVector_iterator;
struct XVector_iterator* XVector_begin(struct XVector* this_vector);
struct XVector_iterator* XVector_end(struct XVector* this_vector);
struct XVector_iterator* XVector_iterator_add(struct XVector* this_vector, struct XVector_iterator*it);
void XVector_iterator_for_each(struct XVector* this_vector, void (*forFund)(void* LPVal));
#endif // ! ITERATOR_H

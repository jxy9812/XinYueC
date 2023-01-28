#ifndef XVECTOR_REVERSE_ITERATOR_H
#define XVECTOR_REVERSE_ITERATOR_H
//反向迭代器
typedef struct XVector_reverse_iterator
{
	char null;
}XVector_reverse_iterator;
struct XVector_iterator* XVector_rbegin(struct XVector* this_vector);
struct XVector_iterator* XVector_rend(struct XVector* this_vector);
struct XVector_iterator* XVector_reverse_iterator_add(struct XVector* this_vector, struct XVector_iterator* it);
#endif // !REVERSE_ITERATOR_H
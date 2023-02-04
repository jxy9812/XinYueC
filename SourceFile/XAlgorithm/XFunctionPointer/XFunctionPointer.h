#ifndef XFUNCTIONPOINTER_H
#define XFUNCTIONPOINTER_H
//小于-比较的函数指针
typedef  bool(*XLess)(const void* LPrevValue, const void* LNextValue);
//相等-比较的函数指针
typedef  bool(*XEquality)(const void* LPrevValue, const void* LNextValue);

#endif // !XFUNCTIONPOINTER_H

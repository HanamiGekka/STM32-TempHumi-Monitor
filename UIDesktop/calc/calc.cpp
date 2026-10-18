#include "calc.h"
#include <stdio.h>

void Calc::input()
{
    printf("Please input two int number:\n");
    scanf("%d%d", &a, &b);
}

void Calc::set(int a_val, int b_val)
{
    a = a_val;
    b = b_val;
}

void Calc::add()
{
    c = a + b;
    output('+');
}

void Calc::sub()
{
    c = a - b;
    output('-');
}

void Calc::mul()
{
   c = a * b;
   output('*');
}

void Calc::div()
{
    //TODO : the value of p2 may be zero
    c = a / b;
    output('/');
}

void Calc::mod()
{
    c = a % b;
}

void Calc::output(char oper)
{
    printf("%d %c %d = %d \n", a, oper, b, c);
}

int Calc::get()
{
    return c;
}



void Add::add()
{
    share = 1;
}

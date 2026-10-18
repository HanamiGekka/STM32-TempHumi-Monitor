#ifndef CALC_H
#define CALC_H

class Calc
{
public:
    // Input
    void input();
    void set(int, int);

    // Process
    void add();
    void sub();
    void mul();
    void div();
    void mod();

    // Output
    void output(char oper);
    int  get();

protected:
    int share;

private:
    int a;
    int b;
    int c;
};

class  Add : public Calc
{
public:
    void add();
};


#endif // CALC_H

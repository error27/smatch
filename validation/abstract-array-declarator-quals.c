#define N 2

void ok1(int []);
void ok2(int [N]);
void ok3(int [const volatile restrict]);
void ok4(int [const volatile restrict N]);
void ok5(int [static N]);
void ok6(int [static const volatile restrict N]);
void ok7(int [const volatile restrict static N]);

void ok1(int a[]);
void ok2(int a[N]);
void ok3(int a[const volatile restrict]);
void ok4(int a[const volatile restrict N]);
void ok5(int a[static N]);
void ok6(int a[static const volatile restrict N]);
void ok7(int a[const volatile restrict static N]);

/*
 * check-name: abstract-array-declarator-quals
 */

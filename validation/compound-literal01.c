struct bfs {
        int a: 2;
        int b: 30;
};

int foo(void)
{
        struct bfs bf = { .a = 1, .b = 2 };
        return (struct bfs[]){bf}[0].b;
}

int bar(void)
{
        struct bfs bf = { .a = 1, .b = 4 };
        return (struct bfs[]){bf, { .a = 3, .b = 6}}[1].b;
}

/*
 * check-name: compound-literal01.c
 * check-command: test-linearize -Wno-decl $file
 *
 * check-known-to-fail
 * check-output-ignore
 * check-output-contains: ret\\..*\\$2
 * check-output-contains: ret\\..*\\$6
 * check-error-end
 */

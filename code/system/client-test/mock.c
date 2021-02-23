#include <stdlib.h>
#include <stdio.h>
int main(void)
{
    for (int i = 1; i <= 3; i++)
    {
        char command[16];
        sprintf(command, "%s%d", "./client a", i);
        system(command);
    }
}
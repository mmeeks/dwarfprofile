/*
 * Trivial test program for dwarfprofile
 *
 * This program should compile with the increment function standalone for the
 * export and also inline within the main() because inline is smaller than the
 * call to the function.
 *
 * 00000034	     qa
 * 00000031	      increment
 * 00000003	      main
 */
volatile int i;

extern void increment();

void increment()
{
  i++;
}

int main()
{
  increment();
  return 0;
}

/*
 * Trivial test program for dwarfprofile
 *
 * This program should compile with the increment and decremnt functions *only*
 * inline within the main() . The increment function should have a lexical
 * scope block which should be accounted for inside the totals for increment.
 *
 * correct output (on amd64) is something like
 *
 * 00000056	     qa
 * 00000030	      decrement
 * 00000023	      increment
 * 00000003	      main
 *
 * output like the following indicates lexical blocks are not being correctly
 * accounted for in their parent
 *
 * 00000048	     qa
 * 00000030	      decrement
 * 00000015	      increment
 * 00000003	      main
 *
 */

volatile int i;
volatile int o;

static void increment(void);
static int decrement(void);

int decrement(void)
{
    return o--;
}

void increment()
{
  i++;
  {
      int a;
      int b;
      a = decrement();
      b = decrement();
      o = b - a;
  }
}

int main()
{
  increment();
  return 0;
}

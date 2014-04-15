/*
 * Trivial test program for dwarfprofile
 *
 * This program should compile with the increment function *only* inline within
 * the main() because inline is smaller than the call to the function. It is
 * not exporting the increment function so the text for that should be omitted.
 *
 * 00000018	     qa
 * 00000015	      increment
 * 00000003	      main
 */

volatile int i;

static void increment();

void increment()
{
  i++;
}

int main()
{
  increment();
  return 0;
}

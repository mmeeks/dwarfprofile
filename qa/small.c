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

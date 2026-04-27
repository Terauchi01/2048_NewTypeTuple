#include <random>
using namespace std;

inline int rand(mt19937 &mt, int N)
{
  uniform_int_distribution<> range(0, N-1);
  return range(mt);
}

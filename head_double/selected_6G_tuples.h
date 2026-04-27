#define AVAIL_TUPLE 8
#define NUM_TUPLE 8
#define TUPLE_SIZE 6
#define ARRAY_LENGTH (VARIATION_TILE * VARIATION_TILE * VARIATION_TILE * VARIATION_TILE * VARIATION_TILE * VARIATION_TILE)
// {0,1,2,3,
//  4,5,6,7,
//  8,9,10,11,
//  12,13,14,15}
int tuples[AVAIL_TUPLE][TUPLE_SIZE] = {
    {0,1,2,4,5,6},
    {1,2,5,6,9,13},
    {0,1,2,3,4,5},
    {0,1,5,6,7,10},
    {0,1,2,5,9,10},
    {0,1,5,9,13,14},
    {0,1,5,8,9,13},
    {0,1,2,4,6,10},
};

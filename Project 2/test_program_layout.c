#define DIM 16

int a[DIM * DIM];
int b[DIM * DIM];
int c[DIM * DIM];

int main() {
    int i, j, k;
    for (i = 0; i < DIM; i++)
        for (j = 0; j < DIM; j++)
            for (k = 0; k < DIM; k++)
                c[i * DIM + j] += a[i * DIM + k] * b[k * DIM + j];
    return 0;
}
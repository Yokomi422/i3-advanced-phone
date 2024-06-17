#include <assert.h>
#include <complex.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <math.h> // Include math.h for mathematical functions

#define NUM_SAMPLES 1000 // Changed from N to NUM_SAMPLES

typedef short sample_t;

void die(char *s) {
    perror(s);
    exit(1);
}

/* fd から 必ず n バイト読み, bufへ書く.
   n バイト未満でEOFに達したら, 残りは0で埋める.
   fd から読み出されたバイト数を返す */
ssize_t read_n(int fd, ssize_t n, void *buf) {
    ssize_t re = 0;
    while (re < n) {
        ssize_t r = read(fd, buf + re, n - re);
        if (r == -1) die("read");
        if (r == 0) break;
        re += r;
    }
    memset(buf + re, 0, n - re);
    return re;
}

/* fdへ, bufからnバイト書く */
ssize_t write_n(int fd, ssize_t n, void *buf) {
    ssize_t wr = 0;
    while (wr < n) {
        ssize_t w = write(fd, buf + wr, n - wr);
        if (w == -1) die("write");
        wr += w;
    }
    return wr;
}

/* 標本(整数)を複素数へ変換 */
void sample_to_complex(sample_t *s, complex double *X, long n) {
    long i;
    for (i = 0; i < n; i++) X[i] = s[i];
}

/* 複素数を標本(整数)へ変換. 虚数部分は無視 */
void complex_to_sample(complex double *X, sample_t *s, long n) {
    long i;
    for (i = 0; i < n; i++) {
        s[i] = creal(X[i]);
    }
}

/* 高速(逆)フーリエ変換;
   w は1のn乗根.
   フーリエ変換の場合   偏角 -2 pi / n
   逆フーリエ変換の場合 偏角  2 pi / n
   xが入力でyが出力.
   xも破壊される
 */
void fft_r(complex double *x, complex double *y, long n, complex double w) {
    if (n == 1) {
        y[0] = x[0];
    } else {
        complex double W = 1.0;
        long i;
        for (i = 0; i < n / 2; i++) {
            y[i] = (x[i] + x[i + n / 2]); /* 偶数行 */
            y[i + n / 2] = W * (x[i] - x[i + n / 2]); /* 奇数行 */
            W *= w;
        }
        fft_r(y, x, n / 2, w * w);
        fft_r(y + n / 2, x + n / 2, n / 2, w * w);
        for (i = 0; i < n / 2; i++) {
            y[2 * i] = x[i];
            y[2 * i + 1] = x[i + n / 2];
        }
    }
}

void fft(complex double *x, complex double *y, long n) {
    long i;
    #ifndef M_PI 3.14159265358979323846
    #define M_PI 3.14159265358979323846
    #endif
    double arg = 2.0 * M_PI / n;
    complex double w = cos(arg) - 1.0j * sin(arg);
    fft_r(x, y, n, w);
    for (i = 0; i < n; i++) y[i] /= n;
}

void ifft(complex double *y, complex double *x, long n) {
    double arg = 2.0 * M_PI / n;
    complex double w = cos(arg) + 1.0j * sin(arg);
    fft_r(y, x, n, w);
}

int pow2check(long num) { // Changed parameter name from N to num
    long n = num; // Using num instead of N
    while (n > 1) {
        if (n % 2) return 0;
        n = n / 2;
    }
    return 1;
}

void print_complex(FILE *wp, complex double *Y, long n) {
    long i;
    for (i = 0; i < n; i++) {
        fprintf(wp, "%ld %f %f %f %f\n",
                i,
                creal(Y[i]), cimag(Y[i]),
                cabs(Y[i]), atan2(cimag(Y[i]), creal(Y[i])));
    }
}

void pitch_shift(double pitch_factor, complex double *Y, long n) {
    complex double *temp = calloc(sizeof(complex double), n);
    int k;
    for (k = 0; k < n; k++) {
        int shifted_index = (int)(k * pitch_factor);
        if (shifted_index < n) {
            temp[shifted_index] = Y[k];
        }
    }
    memcpy(Y, temp, sizeof(complex double) * n);
    free(temp);
}

void process_audio(int fd_in, int fd_out, double pitch_factor) {
    double fs = 44100.0;
    long n = 8192;
    if (!pow2check(n)) {
        fprintf(stderr, "error : n (%ld) not a power of two\n", n);
        exit(1);
    }

    sample_t *buf = calloc(sizeof(sample_t), n);
    complex double *X = calloc(sizeof(complex double), n);
    complex double *Y = calloc(sizeof(complex double), n);

    while (1) {
        /* 標準入力からn個標本を読む */
        ssize_t m = read_n(fd_in, n * sizeof(sample_t), buf);
        if (m == 0) break;
        /* 複素数の配列に変換 */
        sample_to_complex(buf, X, n);
        /* FFT -> Y */
        fft(X, Y, n);

        // Perform pitch shifting
        pitch_shift(pitch_factor, Y, n);

        /* IFFT -> X */
        ifft(Y, X, n);
        /* 標本の配列に変換 */
        complex_to_sample(X, buf, n);
        /* 標準出力へ出力 */
        write_n(fd_out, m, buf);
    }

    free(buf);
    free(X);
    free(Y);
}

int main(int argc, char *argv[]) {
    if (argc > 4 || argc < 3) {
        fprintf(stderr, "Usage: %s <Port Number> <Pitch Factor> [<IP Address>]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port_number = atoi(argv[1]);
    double pitch_factor = atof(argv[2]);

    if (argc == 3) { // Server
        int ss = socket(PF_INET, SOCK_STREAM, 0);
        if (ss == -1) {
            perror("socket creation failed");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_number);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(ss, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
            perror("bind failed");
            close(ss);
            exit(EXIT_FAILURE);
        }

        if (listen(ss, 10) == -1) {
            perror("listen failed");
            close(ss);
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int s = accept(ss, (struct sockaddr *)&client_addr, &len);
        if (s == -1) {
            perror("accept failed");
            close(ss);
            exit(EXIT_FAILURE);
        }

        process_audio(s, STDOUT_FILENO, pitch_factor);

        close(ss);
        close(s);
    } else if (argc == 4) { // Client
        char *IP = argv[3];

        int s = socket(PF_INET, SOCK_STREAM, 0);
        if (s == -1) {
            perror("socket creation failed");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_number);
        if (inet_aton(IP, &addr.sin_addr) == 0) {
            fprintf(stderr, "Invalid IP address: %s\n", IP);
            close(s);
            exit(EXIT_FAILURE);
        }

        if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
            perror("connect failed");
            close(s);
            exit(EXIT_FAILURE);
        }

        process_audio(STDIN_FILENO, s, pitch_factor);

        close(s);
    }

    return 0;
}


//Server : rec -t raw -b 16 -c 1 -e s -r 44100 - | ./voice_change port pf | play -t raw -b 16 -c 1 -e s -r 44100 -
//Client : rec -t raw -b 16 -c 1 -e s -r 44100 - | ./voice_change port pf IP | play -t raw -b 16 -c 1 -e s -r 44100 - 

//server: ./voice_change <Port Number> <Pitch Factor> 
//client: ./voice_change <Port Number> <Pitch Factor> <IP Address>
//pitch factor(pf)は周波数を何倍にするか

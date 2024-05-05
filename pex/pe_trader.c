#include "pe_trader.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <sys/select.h>
#include <stdlib.h>

int sig_received = 0;

// signal handler
void sig_handler(int sig) {
    if (sig == SIGUSR1) {
        sig_received = 1;
    }
}

int main(int argc, char ** argv) {

    //check for invalid arguments
    if (argc < 2) {
        printf("Not enough arguments\n");
        return 1;
    }

    //setup signal handling
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = 0;
    sa.sa_handler = sig_handler;
    sigaction(SIGUSR1, &sa, NULL);
    
    // open FIFO_EXCHANGE and FIFO_TRADER
    //argv[1] = "0";
    char fifo_exchange_name[sizeof("/tmp/pe_exchange_") + sizeof(argv[1]) + 1];
    sprintf(fifo_exchange_name, "/tmp/pe_exchange_%s", argv[1]);
    int fd_exchange = open(fifo_exchange_name, O_RDONLY);

    if(fd_exchange == -1){
        perror("Open FIFO_EXCHANGE error\n");
        return 1;
    }

    char fifo_trader_name[sizeof("/tmp/pe_trader_") + sizeof(argv[1]) + 1];
    sprintf(fifo_trader_name, "/tmp/pe_trader_%s", argv[1]);
    int fd_trader = open(fifo_trader_name, O_WRONLY);

    if(fd_trader == -1){
        perror("Open FIFO_TRADER error\n");
        return 1;
    }

    int count = 0;

    // event loop: 
    while (1) {

        char buffer[1024] = {0};

        // wait for exchange update (MARKET message)
        pause();

        // read message from exchange
        int read_msg = read(fd_exchange, buffer, 1024);
        if(read_msg == -1){
            perror("Read message from pipe error\n");
            return 1;
        }
        else if(read_msg == 0){
            printf("EOF");
        }
        else{
            buffer[read_msg] = '\0';
        }

        // parse message
        char marketstring[12];
        for(int i = 0; i < 11; i++){
            marketstring[i] = buffer[i];
        }
        marketstring[11] = '\0';

        // send order
        if (strcmp(marketstring, "MARKET SELL") == 0){

            char item[10];
            int quantity, price;
            sscanf(buffer, "MARKET SELL %s %d %d;", item, &quantity, &price);

            char buy[1024];
            sprintf(buy, "BUY %d %s %d %d;", count, item, quantity, price);

            if(quantity > 999){
                exit(0);
            }

            //write to exchange
            write(fd_trader, buy, strlen(buy));
            kill(getppid(), SIGUSR1);

            count += 1;
        }

        sig_received = 0;
    }

    close(fd_trader);
    close(fd_exchange);
}

/*
 * COMP2017 - Assignment 3
 * Tongyu Hu
 * Tohu8281
*/

#define MAX_TRADERS 10
#define TRADER_NAME 30
#define _POSIX_C_SOURCE 199309L
#define _POSIX_SOURCE
#define _GNU_SOURCE

#include "pe_trader.h"
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <string.h>
#include <fcntl.h>
#include <sys/select.h>
#include <stdlib.h>
#include <math.h>

//setup global variables
int sig_received = 0;
int commission = 0;
int fd_exchange = 0;
int fd_trader = 0;
int sig_pid;
int num_traders;
int active_traders;
int current_trader_id;
int order_count_buy = 0;
int order_count_sell = 0;
int total_order = 0;
int total_fees = 0;
int new_order_send = 0;
int flag = 0;
int found;

//setup structs
struct trader_items{
    char item[100];
    int totaltraded;
    int quanttraded;
};

struct trader_details{
    pid_t pid;
    int id;
    int fd_exchange;
    int fd_trader;
    char fifo_trader_names[MAX_TRADERS][TRADER_NAME];
	char fifo_exchange_names[MAX_TRADERS][TRADER_NAME];
    struct trader_items traderitems[10];
};

struct order{
    int trader_id;
    int order_id;
    char order_type[5];
    int quantity;
    int price;
    char item[100];
    int count;
};

struct quantityprice{
    int quantity;
    int price;
    int no_order;
    int more_than_one;
    int type;
    int match_order_occurred;
};

struct product{
    char item[100];
    int buy_levels;
    int sell_levels;
    int qindex;
    struct quantityprice quantityprices[10];
};

//setup arrays of structs
struct trader_details trader[5];
struct order order_book_buy[10];
struct order order_book_sell[10];
struct product product_array[10];

//setup functions/signal handlers
int compare_order_prices(const void* a, const void* b) {
    const struct order* order1 = (const struct order*) a;
    const struct order* order2 = (const struct order*) b;
    if (order1->price < order2->price) {
        return -1;
    } else if (order1->price > order2->price) {
        return 1;
    } else {
        // If the prices are equal, compare based on order_id
        if (order1->order_id < order2->order_id) {
            return -1;
        } else if (order1->order_id > order2->order_id) {
            return 1;
        } else {
            return 0;
        }
    }
}

void sig_usr1_handler(int sig, siginfo_t *siginfo, void *context) {
    if (sig == SIGUSR1) {
        // Handle SIGUSR1
        sig_received = 1;
        for(int i = 0; i < num_traders; i++) {
            if (trader[i].pid == siginfo->si_pid) {       
                current_trader_id = trader[i].id; 
                break;
            }
        }
    }
}

void sig_chld_handler(int sig) {
    int status;
    pid_t pid;
    int i;

    pid = waitpid(-1, &status, WNOHANG);

    //If pid received is valid and from trader
    if (pid > 0) {
        for (i = 0; i < num_traders; i++) {
            if (trader[i].pid == pid) {       
                current_trader_id = trader[i].id;     
                fprintf(stdout, "[PEX] Trader %d disconnected\n", trader[i].id);
                active_traders--;
            }
        }

        //close all pipes once traders disconnect
        if(active_traders == 0) {
            fprintf(stdout, "[PEX] Trading completed\n");
            fprintf(stdout, "[PEX] Exchange fees collected: $%d\n", total_fees);
            for (i = 0; i < num_traders; i++) {
                close(trader[i].fd_exchange);
                close(trader[i].fd_trader);
                unlink(trader[i].fifo_trader_names[i]);
                unlink(trader[i].fifo_exchange_names[i]);
            }
            exit(0);
        }
    }
}

int main(int argc, char **argv) {
    // Check for invalid arguments
    if (argc < 2) {
        printf("Not enough arguments\n");
        return 1;
    }

    // Setup signal handling for SIGUSR1
    struct sigaction sa_usr1;
    memset(&sa_usr1, 0, sizeof(sa_usr1));
    sa_usr1.sa_flags = SA_SIGINFO | SA_RESTART | SA_NOCLDSTOP;
    sa_usr1.sa_sigaction = sig_usr1_handler;
    sigaction(SIGUSR1, &sa_usr1, NULL);

    // Setup signal handling for SIGCHLD
    struct sigaction sa_chld;
    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = sig_chld_handler;
    sigaction(SIGCHLD, &sa_chld, NULL);

    fprintf(stdout, "[PEX] Starting\n");

    //read product file
	FILE* product_file = fopen(argv[1], "r");
	if(product_file == NULL){
		perror("Open product file error\n");
		return 1;
	}
    
    char products[100];
    int product_count = 0;    

    //add products to array
    while(fgets(products, 100, product_file) != NULL){
        products[strcspn(products, "\n")] = 0;
        strcpy(product_array[product_count].item, products);
        product_array[product_count].buy_levels = 0;
        product_array[product_count].sell_levels = 0;
        product_array[product_count].qindex = 0;
        product_count += 1;
    }

    fclose(product_file);

    if(product_count == 2){
        fprintf(stdout, "[PEX] Trading %d products: %s\n", product_count - 1, 
        product_array[1].item);
    }
    else if(product_count == 3){
        fprintf(stdout, "[PEX] Trading %d products: %s %s\n", product_count - 1, 
        product_array[1].item, product_array[2].item);
    }
    else if(product_count == 4){
        fprintf(stdout, "[PEX] Trading %d products: %s %s %s\n", product_count - 1, 
        product_array[1].item, product_array[2].item, product_array[3].item);
    }
    
    //setup active and total number of traders
    num_traders = argc - 2;
    active_traders = num_traders;

    //create fifo, open pipes for each trader
    for (int i = 0; i < num_traders; i++) {

        trader[i].id = i;

        sprintf(trader[i].fifo_trader_names[i], "/tmp/pe_trader_%d", i);
		sprintf(trader[i].fifo_exchange_names[i], "/tmp/pe_exchange_%d", i);

        for(int x = 0; x < product_count; x++){
            strcpy(trader[i].traderitems[x].item, product_array[x + 1].item);
        }

		if(mkfifo(trader[i].fifo_exchange_names[i], 0777) < 0){
			perror("mkfifo");
			return 1;
		}
        fprintf(stdout, "[PEX] Created FIFO %s\n", trader[i].fifo_exchange_names[i]);

        if (mkfifo(trader[i].fifo_trader_names[i], 0777) < 0) {
            perror("mkfifo");
            return 1;
        }

        fprintf(stdout, "[PEX] Created FIFO %s\n", trader[i].fifo_trader_names[i]);
        
        //fork process to create child trader processes
        pid_t pid = fork();
        trader[i].pid = pid;
        if(pid < 0){
            perror("fork");
            return 1;
        }
        else if(pid == 0){
            fprintf(stdout, "[PEX] Starting trader %d (%s)\n", i, argv[i+2]);

            char trader_id_str[TRADER_NAME];
            sprintf(trader_id_str, "%d", i);

            execl(argv[i+2], argv[i+2], trader_id_str, NULL);
            perror("execl");
            return 1;
        }
        else{
            trader[i].fd_exchange = open(trader[i].fifo_exchange_names[i], O_WRONLY);
            if(trader[i].fd_exchange < 0){
                perror("open");
                return 1;
            }
            fprintf(stdout, "[PEX] Connected to %s\n", trader[i].fifo_exchange_names[i]);

            trader[i].fd_trader = open(trader[i].fifo_trader_names[i], O_RDONLY);
            if(trader[i].fd_trader < 0){
                perror("open");
                return 1;
            }
            fprintf(stdout, "[PEX] Connected to %s\n", trader[i].fifo_trader_names[i]);
            
            write(trader[i].fd_exchange, "MARKET OPEN;", strlen("MARKET OPEN;"));
            kill(pid, SIGUSR1);
            
        }
    }

    //main while loop for exchange
    while(1){

        pause();

        if(sig_received != 1){
            continue;
        }else{
            //read message sent from trader
            char buf[256];
            int n = read(trader[current_trader_id].fd_trader, buf, sizeof(buf));
            if(n == -1){
                perror("Read message from pipe error");
                return 1;
            }
            else{
                buf[n] = '\0';

                //grab buy order string
                char buy[4];
                for(int i = 0; i < 3; i++){
                    buy[i] = buf[i];
                }
                buy[3] = '\0';

                //grab sell order string
                char sell[5];
                for(int i = 0; i < 4; i++){
                    sell[i] = buf[i];
                }
                sell[4] = '\0';

                //deal with buy orders
                if(strcmp(buy, "BUY") == 0){

                    //grab details of the order
                    char item[20];
                    int count, quantity, price;
                    sscanf(buf, "BUY %d %s %d %d", &count, item, &quantity, &price);

                    //send accepted message
                    char accept_id[1024];
                    memset(accept_id, 0, strlen(accept_id));
                    sprintf(accept_id, "ACCEPTED %d;", count);
                    write(trader[current_trader_id].fd_exchange, accept_id, strlen(accept_id));
                    kill(trader[current_trader_id].pid, SIGUSR1);

                    //add order to order book
                    order_book_buy[order_count_buy].trader_id = current_trader_id;
                    strcpy(order_book_buy[order_count_buy].item, item);
                    order_book_buy[order_count_buy].quantity = quantity;
                    order_book_buy[order_count_buy].price = price;
                    order_book_buy[order_count_buy].order_id = order_count_buy;

                    //parse command
                    fprintf(stdout, "[PEX] [T%d] Parsing command: <BUY %d %s %d %d>\n", 
                    current_trader_id, count, item, quantity, price);

                    //check if there is an order that matches
                    for(int i = 0; i < order_count_sell; i++){
                        if(strcmp(order_book_sell[i].item, item) == 0){
                            if(price >= order_book_sell[i].price && quantity > order_book_sell[i].quantity){
                                int values = order_book_sell[i].quantity * order_book_sell[i].price;
                                int fees = floor((values * 0.01) + 0.5);
                                total_fees += fees;

                                //send fill message and print out matching order
                                fprintf(stdout, "[PEX] Match: Order %d [T%d], New Order %d [T%d], value: $%d, fee: $%d.\n", 
                                order_book_sell[i].order_id, order_book_sell[i].trader_id, order_book_buy[order_count_buy].order_id, 
                                current_trader_id, values, fees);

                                char fill_own[1024];
                                sprintf(fill_own, "FILL %d %d;", order_count_sell, order_book_sell[i].quantity);
                                kill(trader[current_trader_id].pid, SIGUSR1);
                                write(trader[current_trader_id].fd_exchange, fill_own, strlen(fill_own));

                                //find the relevant product in the trader_items array
                                for(int y = 0; y < product_count; y++){
                                    if(strcmp(item, trader[current_trader_id].traderitems[y].item) == 0){
                                        trader[current_trader_id].traderitems[y].totaltraded -= (values + fees);
                                        trader[current_trader_id].traderitems[y].quanttraded += order_book_sell[i].quantity;
                                    }
                                    if(strcmp(order_book_sell[i].item, trader[order_book_sell[i].trader_id].traderitems[y].item) == 0){
                                        trader[order_book_sell[i].trader_id].traderitems[y].totaltraded += values;
                                        trader[order_book_sell[i].trader_id].traderitems[y].quanttraded -= order_book_sell[i].quantity;
                                    }
                                }

                                //change relevant product details in product array
                                for(int z = 0; z < product_count; z++){
                                    if(strcmp(item, product_array[z].item) == 0){
                                        product_array[z].buy_levels += 1;
                                        product_array[z].sell_levels -= 1;
                                        product_array[z].quantityprices[0].quantity = quantity - order_book_sell[i].quantity;
                                        product_array[z].quantityprices[0].match_order_occurred = 1;
                                        product_array[z].quantityprices[0].type = 1;
                                        flag = 1;
                                    }
                                }
                            }
                        }
                    }

                    fprintf(stdout, "[PEX]\t--ORDERBOOK--\n");
                    if(total_order == 0){
                        //cycle through the products to find matching one and add details
                        for(int i = 1; i < product_count; i++){
                            if(strcmp(item, product_array[i].item) == 0){
                                product_array[i].buy_levels = 1;
                                product_array[i].sell_levels = 0;
                                product_array[i].qindex = 0;
                                product_array[i].quantityprices[product_array[i].qindex].quantity = quantity;
                                product_array[i].quantityprices[product_array[i].qindex].price = price;
                                product_array[i].quantityprices[product_array[i].qindex].no_order = 1;
                                product_array[i].quantityprices[product_array[i].qindex].type = 1;
                                product_array[i].qindex++;
                            }
                        }
                        //print the product levels etc once details filled
                        fprintf(stdout, "[PEX]\tProduct: %s; Buy levels: %d; Sell levels: %d\n", 
                        product_array[1].item, product_array[1].buy_levels, product_array[1].sell_levels);

                        //loop to print the number of different prices
                        for(int y = 0; y < product_array[1].qindex; y++){
                            fprintf(stdout, "[PEX]\t\tBUY %d @ $%d (%d order)\n", 
                            product_array[1].quantityprices[y].quantity, product_array[1].quantityprices[y].price, 
                            product_array[1].quantityprices[y].no_order);
                        }

                        //loop for rest of the products to print out details
                        for(int z = 1; z < product_count; z++){
                            if(strcmp(item, product_array[z].item) != 0){
                                fprintf(stdout, "[PEX]\tProduct: %s; Buy levels: %d; Sell levels: %d\n", 
                                product_array[z].item, product_array[z].buy_levels, product_array[z].sell_levels);
                            }
                        }
                    }
                    else{
                        //loop to find matching product and add details
                        for(int i = 1; i < product_count; i++){
                            if(strcmp(item, product_array[i].item) == 0){
                                int found = 0;
                                for(int x = 0; x < product_array[i].qindex; x++){
                                    if(price == product_array[i].quantityprices[x].price){
                                        if(product_array[i].quantityprices[x].match_order_occurred == 1){
                                            found = 1;
                                        }
                                        else{
                                            product_array[i].quantityprices[x].quantity += quantity;
                                            product_array[i].quantityprices[x].no_order++;
                                            product_array[i].quantityprices[x].more_than_one = 1;
                                            found = 1;
                                        }
                                    }
                                }
                                //if product not found, add new price, type = 1 = buy
                                if(found == 0){
                                    product_array[i].quantityprices[product_array[i].qindex].quantity = quantity;
                                    product_array[i].quantityprices[product_array[i].qindex].price = price;
                                    product_array[i].quantityprices[product_array[i].qindex].no_order = 1;
                                    product_array[i].quantityprices[product_array[i].qindex].type = 1;
                                    product_array[i].qindex++;
                                    product_array[i].buy_levels++;
                                }
                            }
                        }

                        //print out relevant buy and sell levels, depending on whether match occurred and if more than one price
                        for(int p = 1; p < product_count; p++){
                            fprintf(stdout, "[PEX]\tProduct: %s; Buy levels: %d; Sell levels: %d\n", 
                            product_array[p].item, product_array[p].buy_levels, product_array[p].sell_levels);

                            for(int y = product_array[p].qindex - 1; y >= 0; y--){
                                if(product_array[p].quantityprices[y].more_than_one == 1){
                                    if(product_array[p].quantityprices[y].type == 1){
                                        fprintf(stdout, "[PEX]\t\tBUY %d @ $%d (%d orders)\n", 
                                        product_array[p].quantityprices[y].quantity, product_array[p].quantityprices[y].price, 
                                        product_array[p].quantityprices[y].no_order);
                                    }
                                    else{
                                        fprintf(stdout, "[PEX]\t\tSELL %d @ $%d (%d orders)\n", 
                                        product_array[p].quantityprices[y].quantity, product_array[p].quantityprices[y].price, 
                                        product_array[p].quantityprices[y].no_order);
                                    }
                                }
                                else{
                                    if(product_array[p].quantityprices[y].type == 1){
                                        fprintf(stdout, "[PEX]\t\tBUY %d @ $%d (%d order)\n", 
                                        product_array[p].quantityprices[y].quantity, product_array[p].quantityprices[y].price, 
                                        product_array[p].quantityprices[y].no_order);
                                    }
                                    else{
                                        fprintf(stdout, "[PEX]\t\tSELL %d @ $%d (%d order)\n", 
                                        product_array[p].quantityprices[y].quantity, product_array[p].quantityprices[y].price, 
                                        product_array[p].quantityprices[y].no_order);
                                    }
                                }
                            }
                        }
                    }

                    //print positions for each trader and their items
                    fprintf(stdout, "[PEX]\t--POSITIONS--\n");

                    for(int i = 0; i < num_traders; i++){
                        fprintf(stdout, "[PEX]\tTrader %d: ", i);
                        for(int j = 1; j < product_count; j++){
                            if(product_count - 1 == j){
                                for(int y = 0; y < product_count; y++){
                                    if(strcmp(product_array[j].item, trader[i].traderitems[y].item) == 0){
                                        fprintf(stdout, "%s %d ($%d)\n", product_array[j].item, trader[i].traderitems[y].quanttraded, trader[i].traderitems[y].totaltraded);
                                    }
                                }
                            }else{
                                for(int y = 0; y < product_count; y++){
                                    if(strcmp(product_array[j].item, trader[i].traderitems[y].item) == 0){
                                        fprintf(stdout, "%s %d ($%d), ", product_array[j].item, trader[i].traderitems[y].quanttraded, trader[i].traderitems[y].totaltraded);
                                    }
                                }
                            }
                        }
                    }
                    
                    //flag for final closing of traders and exchange
                    if(flag == 1){
                        for (int i = 0; i < num_traders; i++) {
                            close(trader[i].fd_exchange);
                            close(trader[i].fd_trader);
                            unlink(trader[i].fifo_trader_names[i]);
                            unlink(trader[i].fifo_exchange_names[i]);
                        }
                        exit(0);     
                    }

                    //send market buy message to all other traders
                    char market[1024];
                    sprintf(market, "MARKET BUY %s %d %d;", item, quantity, price);

                    for(int trader_dispense = 0; trader_dispense < num_traders; trader_dispense++){
                        if(trader_dispense != current_trader_id){
                            write(trader[trader_dispense].fd_exchange, market, strlen(market));
                            kill(trader[trader_dispense].pid, SIGUSR1);
                        }
                    }

                    order_count_buy += 1;
                    total_order += 1;
                }

                //deal with relevant sell orders
                else if(strcmp(sell, "SELL") == 0){

                    //obtain all data sent from trader for order
                    new_order_send = 0;
                    char item_sell[10];
                    int count_sell, quantity_sell, price_sell;
                    sscanf(buf, "SELL %d %s %d %d", &count_sell, item_sell, &quantity_sell, &price_sell);

                    //send accepted message to trader
                    char accept_id[1024];
                    sprintf(accept_id, "ACCEPTED %d;", count_sell);
                    kill(trader[current_trader_id].pid, SIGUSR1);
                    write(trader[current_trader_id].fd_exchange, accept_id, strlen(accept_id));

                    //update order book with new order
                    order_book_sell[order_count_sell].trader_id = current_trader_id;
                    strcpy(order_book_sell[order_count_sell].item, item_sell);
                    order_book_sell[order_count_sell].quantity = quantity_sell;
                    order_book_sell[order_count_sell].price = price_sell;
                    order_book_sell[order_count_sell].order_id = order_count_sell;
                    
                    //print order to exchange
                    fprintf(stdout, "[PEX] [T%d] Parsing command: <SELL %d %s %d %d>\n", 
                    current_trader_id, count_sell, item_sell, quantity_sell, price_sell);

                    //send market sell message to other traders
                    char market[1024];
                    sprintf(market, "MARKET SELL %s %d %d;", item_sell, quantity_sell, price_sell);

                    for(int trader_dispense = 0; trader_dispense < num_traders; trader_dispense++){
                        if(trader_dispense != current_trader_id){
                            write(trader[trader_dispense].fd_exchange, market, strlen(market));
                            kill(trader[trader_dispense].pid, SIGUSR1);
                        }
                    }

                    //check for matching orders in buy order book
                    for(int i = order_count_buy - 1; i >= 0; i--){
                        if(price_sell <= order_book_buy[i].price && quantity_sell >= order_book_buy[i].quantity){
                            int value = order_book_buy[i].quantity * order_book_buy[i].price;
                            int fee = floor((value * 0.01) + 0.5);
                            total_fees += fee;

                            char fill_own[1024];
                            char fill_other[1024];

                            //print match depending on type of order_id and also send fill message to trader
                            if(order_book_buy[i].order_id == 2){
                                fprintf(stdout, "[PEX] Match: Order %d [T%d], New Order %d [T%d], value: $%d, fee: $%d.\n", 
                                order_book_buy[i - 1].order_id, order_book_buy[i].trader_id, order_book_sell[order_count_sell].order_id, 
                                current_trader_id, value, fee);

                                sprintf(fill_other, "FILL %d %d;", order_book_buy[i - 1].order_id, order_book_buy[i].quantity);
                                sprintf(fill_own, "FILL %d %d;", order_count_sell, order_book_buy[i].quantity);
                                kill(trader[current_trader_id].pid, SIGUSR1);
                                write(trader[current_trader_id].fd_exchange, fill_own, strlen(fill_own));
                                kill(trader[order_book_buy[i].trader_id].pid, SIGUSR1);
                                write(trader[order_book_buy[i].trader_id].fd_exchange, fill_other, strlen(fill_other));
                    
                            }else if (order_book_buy[i].order_id == 1){
                                fprintf(stdout, "[PEX] Match: Order %d [T%d], New Order %d [T%d], value: $%d, fee: $%d.\n", 
                                order_book_buy[i + 1].order_id, order_book_buy[i].trader_id, order_book_sell[order_count_sell].order_id, 
                                current_trader_id, value, fee);

                                sprintf(fill_other, "FILL %d %d;", order_book_buy[i + 1].order_id, order_book_buy[i].quantity);
                                sprintf(fill_own, "FILL %d %d;", order_count_sell, order_book_buy[i].quantity);
                                kill(trader[current_trader_id].pid, SIGUSR1);
                                write(trader[current_trader_id].fd_exchange, fill_own, strlen(fill_own));
                                kill(trader[order_book_buy[i].trader_id].pid, SIGUSR1);
                                write(trader[order_book_buy[i].trader_id].fd_exchange, fill_other, strlen(fill_other));
                
                            }else{
                                fprintf(stdout, "[PEX] Match: Order %d [T%d], New Order %d [T%d], value: $%d, fee: $%d.\n", 
                                order_book_buy[i].order_id, order_book_buy[i].trader_id, order_book_sell[order_count_sell].order_id, 
                                current_trader_id, value, fee);

                                sprintf(fill_other, "FILL %d %d;", order_book_buy[i].order_id, order_book_buy[i].quantity);
                                sprintf(fill_own, "FILL %d %d;", order_count_sell, order_book_buy[i].quantity);
                                kill(trader[current_trader_id].pid, SIGUSR1);
                                write(trader[current_trader_id].fd_exchange, fill_own, strlen(fill_own));
                                kill(trader[order_book_buy[i].trader_id].pid, SIGUSR1);
                                write(trader[order_book_buy[i].trader_id].fd_exchange, fill_other, strlen(fill_other));
                            }

                            //update trader items and relevant details
                            for(int y = 0; y < product_count; y++){
                                if(strcmp(item_sell, trader[current_trader_id].traderitems[y].item) == 0){
                                    trader[current_trader_id].traderitems[y].totaltraded += (value - fee);
                                    trader[current_trader_id].traderitems[y].quanttraded -= order_book_buy[i].quantity;
                                }
                                if(strcmp(order_book_buy[i].item, trader[order_book_buy[i].trader_id].traderitems[y].item) == 0){
                                    trader[order_book_buy[i].trader_id].traderitems[y].totaltraded -= value;
                                    trader[order_book_buy[i].trader_id].traderitems[y].quanttraded += order_book_buy[i].quantity;
                                }
                            }

                            //update product details
                            for(int z = 0; z < product_count; z++){
                                if(strcmp(item_sell, product_array[z].item) == 0){
                                    for(int q = 0; q < product_array[z].qindex; q++){
                                        if(order_book_buy[i].price == product_array[z].quantityprices[q].price){
                                            product_array[z].quantityprices[q].quantity = 0;
                                            product_array[z].quantityprices[q].no_order = 0;
                                            product_array[z].quantityprices[q].price = 0;
                                            product_array[z].buy_levels -= 1;
                                        }
                                    }
                                }
                            }
                            order_book_buy[i].price = 0;
                            quantity_sell -= order_book_buy[i].quantity;

                        //check if there is insufficient quantity to satisfy complete order
                        }else if(price_sell <= order_book_buy[i].price && quantity_sell <= order_book_buy[i].quantity && quantity_sell > 0){
                            int value = quantity_sell * order_book_buy[i].price;
                            int fee = floor((value * 0.01) + 0.5);
                            total_fees += fee;

                            //match order and print details
                            fprintf(stdout, "[PEX] Match: Order %d [T%d], New Order %d [T%d], value: $%d, fee: $%d.\n", 
                            order_book_buy[i].order_id, order_book_buy[i].trader_id, order_book_sell[order_count_sell].order_id, 
                            current_trader_id, value, fee);

                            //send fill messages to traders
                            char fill_own[1024];
                            char fill_other[1024];
                            sprintf(fill_other, "FILL %d %d;", order_book_buy[i].order_id, quantity_sell);
                            sprintf(fill_own, "FILL %d %d;", order_count_sell, quantity_sell);
                            kill(trader[current_trader_id].pid, SIGUSR1);
                            write(trader[current_trader_id].fd_exchange, fill_own, strlen(fill_own));
                            kill(trader[order_book_buy[i].trader_id].pid, SIGUSR1);
                            write(trader[order_book_buy[i].trader_id].fd_exchange, fill_other, strlen(fill_other));

                            //update trader items and relevant details
                            for(int y = 0; y < product_count; y++){
                                if(strcmp(item_sell, trader[current_trader_id].traderitems[y].item) == 0){
                                    trader[current_trader_id].traderitems[y].totaltraded += (value - fee);
                                    trader[current_trader_id].traderitems[y].quanttraded -= quantity_sell;
                                }
                                if(strcmp(order_book_buy[i].item, trader[order_book_buy[i].trader_id].traderitems[y].item) == 0){
                                    trader[order_book_buy[i].trader_id].traderitems[y].totaltraded -= value;
                                    trader[order_book_buy[i].trader_id].traderitems[y].quanttraded += quantity_sell;
                                }

                            }

                            //update product details
                            for(int z = 0; z < product_count; z++){
                                if(strcmp(item_sell, product_array[z].item) == 0){
                                    for(int q = 0; q < product_array[z].qindex; q++){
                                        if(order_book_buy[i].price == product_array[z].quantityprices[q].price){
                                            product_array[z].quantityprices[q].quantity -= quantity_sell;
                                            if(product_array[z].quantityprices[q].no_order > 1){
                                                product_array[z].quantityprices[q].no_order -= 2;
                                            }
                                            product_array[z].quantityprices[q].more_than_one = 0;
                                            product_array[z].quantityprices[q].match_order_occurred = 1;
                                        }
                                    }
                                }
                            }

                            order_book_buy[i].quantity -= quantity_sell;
                            new_order_send = 1;
                        }
                    }



                    fprintf(stdout, "[PEX]\t--ORDERBOOK--\n");
                    if(total_order == 0){
                        //cycle through the products to find matching one and add details
                        for(int i = 1; i < product_count; i++){
                            if(strcmp(item_sell, product_array[i].item) == 0){
                                product_array[i].sell_levels = 1;
                                product_array[i].qindex = 0;
                                product_array[i].quantityprices[product_array[i].qindex].quantity = quantity_sell;
                                product_array[i].quantityprices[product_array[i].qindex].price = price_sell;
                                product_array[i].quantityprices[product_array[i].qindex].no_order = 1;
                                product_array[i].quantityprices[product_array[i].qindex].type = 0;
                                product_array[i].qindex++;
                            }
                        }
                        //print the product levels etc once details filled
                        fprintf(stdout, "[PEX]\tProduct: %s; Buy levels: %d; Sell levels: %d\n", 
                        product_array[1].item, product_array[1].buy_levels, product_array[1].sell_levels);

                        //loop to print the number of different prices
                        for(int y = 0; y < product_array[1].qindex; y++){
                            fprintf(stdout, "[PEX]\t\tSELL %d @ $%d (%d order)\n", 
                            product_array[1].quantityprices[y].quantity, product_array[1].quantityprices[y].price, 
                            product_array[1].quantityprices[y].no_order);
                        }

                        //loop for rest of the products to print out details
                        for(int z = 1; z < product_count; z++){
                            if(strcmp(item_sell, product_array[z].item) != 0){
                                fprintf(stdout, "[PEX]\tProduct: %s; Buy levels: %d; Sell levels: %d\n", 
                                product_array[z].item, product_array[z].buy_levels, product_array[z].sell_levels);
                            }
                        }
                    }
                    else{
                        //loop to find matching product and add details
                        for(int i = 1; i < product_count; i++){
                            if(strcmp(item_sell, product_array[i].item) == 0){
                                int found = 0;
                                for(int x = 0; x < product_array[i].qindex; x++){
                                    if(price_sell == product_array[i].quantityprices[x].price){
                                        if(product_array[i].quantityprices[x].match_order_occurred == 1){
                                            found = 1;
                                        }
                                        else{
                                        product_array[i].quantityprices[x].quantity += quantity_sell;
                                        product_array[i].quantityprices[x].no_order++;
                                        product_array[i].quantityprices[x].more_than_one = 1;
                                        found = 1;
                                        }
                                    }
                                }
                                //if product not found, add new price and details
                                if(found == 0 && new_order_send == 0){
                                    product_array[i].quantityprices[product_array[i].qindex].quantity = quantity_sell;
                                    product_array[i].quantityprices[product_array[i].qindex].price = price_sell;
                                    product_array[i].quantityprices[product_array[i].qindex].no_order = 1;
                                    product_array[i].quantityprices[product_array[i].qindex].type = 0;
                                    product_array[i].qindex++;
                                    product_array[i].sell_levels++;
                                }
                            }
                        }

                        //loop to print out product details and relevant levels
                        for(int p = 1; p < product_count; p++){
                            fprintf(stdout, "[PEX]\tProduct: %s; Buy levels: %d; Sell levels: %d\n", 
                            product_array[p].item, product_array[p].buy_levels, product_array[p].sell_levels);

                            for(int y = product_array[p].qindex - 1; y >= 0; y--){
                                if(product_array[p].quantityprices[y].more_than_one == 1 && product_array[p].quantityprices[y].price != 0){
                                    if(product_array[p].quantityprices[y].type == 1){
                                        fprintf(stdout, "[PEX]\t\tBUY %d @ $%d (%d orders)\n", 
                                        product_array[p].quantityprices[y].quantity, product_array[p].quantityprices[y].price, 
                                        product_array[p].quantityprices[y].no_order);
                                    }
                                    else{
                                        fprintf(stdout, "[PEX]\t\tSELL %d @ $%d (%d orders)\n", 
                                        product_array[p].quantityprices[y].quantity, product_array[p].quantityprices[y].price, 
                                        product_array[p].quantityprices[y].no_order);
                                    }
                                }
                                else{
                                    if(product_array[p].quantityprices[y].price == 0){
                                        continue;
                                    }
                                    else{
                                        if(product_array[p].quantityprices[y].type == 1){
                                            fprintf(stdout, "[PEX]\t\tBUY %d @ $%d (%d order)\n", 
                                            product_array[p].quantityprices[y].quantity, product_array[p].quantityprices[y].price, 
                                            product_array[p].quantityprices[y].no_order);
                                        }
                                        else{
                                            fprintf(stdout, "[PEX]\t\tSELL %d @ $%d (%d order)\n", 
                                            product_array[p].quantityprices[y].quantity, product_array[p].quantityprices[y].price, 
                                            product_array[p].quantityprices[y].no_order);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    //loop to find matching product and print out details for each trader
                    fprintf(stdout, "[PEX]\t--POSITIONS--\n");

                    for(int i = 0; i < num_traders; i++){
                        fprintf(stdout, "[PEX]\tTrader %d: ", i);
                        for(int j = 1; j < product_count; j++){
                            if(product_count - 1 == j){
                                for(int y = 0; y < product_count; y++){
                                    if(strcmp(product_array[j].item, trader[i].traderitems[y].item) == 0){
                                        fprintf(stdout, "%s %d ($%d)\n", product_array[j].item, trader[i].traderitems[y].quanttraded, trader[i].traderitems[y].totaltraded);
                                    }
                                }
                            }
                            else{
                                for(int y = 0; y < product_count; y++){
                                    if(strcmp(product_array[j].item, trader[i].traderitems[y].item) == 0){
                                        fprintf(stdout, "%s %d ($%d), ", product_array[j].item, trader[i].traderitems[y].quanttraded, trader[i].traderitems[y].totaltraded);
                                    }
                                }
                            }
                        }
                    }

                    order_count_sell += 1;
                    total_order += 1;
                }
            }
            sig_received = 0;
        }
    }
    
    //close final file descriptors and unlink fifos
    for(int i = 0; i < num_traders; i++){
        close(trader[i].fd_exchange);
        close(trader[i].fd_trader);
        unlink(trader[i].fifo_trader_names[i]);
        unlink(trader[i].fifo_exchange_names[i]);
    }
}
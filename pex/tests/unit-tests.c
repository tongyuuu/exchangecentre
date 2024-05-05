#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "pe_exchange.c" 

//global variables used in the code snippet
int mocked_write_result;
int mocked_kill_result;
int current_trader_id;
int count;
int write(int fd, const void *buf, size_t count);
int kill(pid_t pid, int sig);
int mocked_mkfifo_result;
int mocked_open_result_exchange;
int mocked_open_result_trader;
int mkfifo(const char *pathname, mode_t mode);
int open(const char *pathname, int flags, ...);

//mock write function
int mock_write(int fd, const void *buf, size_t count) {
    check_expected(fd); 
    check_expected_ptr(buf);  
    check_expected(count);  
    return mocked_write_result;  
}

//mock kill function
int mock_kill(pid_t pid, int sig) {
    check_expected(pid);  
    check_expected(sig);  
    return mocked_kill_result;  
}

//mock mkfifo function
int mock_mkfifo(const char *pathname, mode_t mode) {
    check_expected_ptr(pathname);  
    check_expected(mode);  
    return mocked_mkfifo_result;  
}

//mock open function
int mock_open(const char *pathname, int flags, ...) {
    check_expected_ptr(pathname);  
    check_expected(flags);  
    return (flags == O_WRONLY) ? mocked_open_result_exchange : mocked_open_result_trader;  
}

//test case for sending accepted message
void test_send_accepted_message(void **state) {
    //set the mocked values
    int expected_fd = 123;  
    const char *expected_message = "ACCEPTED 5;";  
    size_t expected_count = strlen(expected_message);
    mocked_write_result = expected_count;  
    mocked_kill_result = 0; 

    //set expectations for the write mock
    expect_value(write, fd, expected_fd);
    expect_memory(write, buf, expected_message, expected_count);
    expect_value(write, count, expected_count);
    will_return(write, mocked_write_result);

    //set expectations for the kill mock
    expect_value(kill, sig, SIGUSR1);
    will_return(kill, mocked_kill_result);

    //call the function that sends the accepted message
    send_accepted_message();
}

//test case for creating FIFO and opening pipes
void test_create_fifo_and_open_pipes(void **state) {
    int num_traders = 3;
    mocked_mkfifo_result = 0;  
    mocked_open_result_exchange = 123;  
    mocked_open_result_trader = 456;  

    //set expectations for the mkfifo mock
    for (int i = 0; i < num_traders; i++) {
        expect_string(mkfifo, pathname, "/tmp/pe_exchange_0");  
        expect_value(mkfifo, mode, 0777);
        will_return(mkfifo, mocked_mkfifo_result);
        expect_string(mkfifo, pathname, "/tmp/pe_trader_0");  
        expect_value(mkfifo, mode, 0777);
        will_return(mkfifo, mocked_mkfifo_result);
    }

    //set expectations for the open mock
    for (int i = 0; i < num_traders; i++) {
        expect_string(open, pathname, "/tmp/pe_exchange_0");  
        expect_value(open, flags, O_WRONLY);
        will_return(open, mocked_open_result_exchange);
        expect_string(open, pathname, "/tmp/pe_trader_0"); 
        expect_value(open, flags, O_RDONLY);
        will_return(open, mocked_open_result_trader);
    }

    //call the function that creates FIFOs and opens pipes
    int result = create_fifo_and_open_pipes(num_traders);

    //check the result
    assert_int_equal(result, 0);  
}

void test_order_initialization(void **state) {
    struct order o;

    //initialize the order struct
    o.trader_id = 1;
    o.order_id = 12345;
    strcpy(o.order_type, "BUY");
    o.quantity = 100;
    o.price = 500;
    strcpy(o.item, "Apple");
    o.count = 1;

    //perform assertions to verify the initialization
    assert_int_equal(o.trader_id, 1);
    assert_int_equal(o.order_id, 12345);
    assert_string_equal(o.order_type, "BUY");
    assert_int_equal(o.quantity, 100);
    assert_int_equal(o.price, 500);
    assert_string_equal(o.item, "Apple");
    assert_int_equal(o.count, 1);
}

//test case for quantityprice struct
void test_quantityprice(void **state) {
    struct quantityprice qp;

    //initialize the quantityprice struct
    qp.quantity = 100;
    qp.price = 500;
    qp.no_order = 0;
    qp.more_than_one = 1;
    qp.type = 2;
    qp.match_order_occurred = 1;

    //perform assertions to verify the initialization
    assert_int_equal(qp.quantity, 100);
    assert_int_equal(qp.price, 500);
    assert_int_equal(qp.no_order, 0);
    assert_int_equal(qp.more_than_one, 1);
    assert_int_equal(qp.type, 2);
    assert_int_equal(qp.match_order_occurred, 1);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_order_initialization),
        cmocka_unit_test(test_quantityprice),
        cmocka_unit_test(test_create_fifo_and_open_pipes),
        cmocka_unit_test(test_send_accepted_message),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}

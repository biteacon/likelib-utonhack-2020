#include <boost/test/unit_test.hpp>

#include "base/big_num.hpp"

#include <string>
#include <sstream>

BOOST_AUTO_TEST_CASE(BigNum_constructor)
{
    base::Uint256 num1(3u);
    BOOST_CHECK(num1 == base::Uint256(3u));

    base::Uint256 num2(765ll);
    BOOST_CHECK(num2 == base::Uint256(765ll));

    base::Uint256 num3("654321");
    BOOST_CHECK(num3 == base::Uint256("654321"));

    bool res = true; 
    for(uint32_t i = 0; i < 1000; i++){
        base::Uint256 num(i);
        res = res && (num == base::Uint256(i));
    }
    BOOST_CHECK(res);

    res = true; 
    for(uint32_t i = 0; i < 1000; i++){
        base::Uint256 num(std::to_string(i));
        res = res && (num == base::Uint256(i));
    }
    BOOST_CHECK(res);

    /*std::string str_num;
    for(uint32_t i = 0; i < 256; i++){
           str_num += '1';
    }
    base::Uint256 num256(str_num);
    BOOST_CHECK(num256 == base::Uint256(str_num));*/

    base::Uint512 num4(987u);
    BOOST_CHECK(num4 == base::Uint512(987u));

    base::Uint512 num5(111ll);
    BOOST_CHECK(num5 == base::Uint512(111ll));

    base::Uint512 num6("123456");
    BOOST_CHECK(num6 == base::Uint512("123456"));

    res = true;
    for(uint32_t i = 1000; i < 2000; i++){
        base::Uint512 num(i);
        res = res && (num == base::Uint512(i));
    }
    BOOST_CHECK(res);
    
    res = true;
    for(uint32_t i = 1000; i < 2000; i++){
        base::Uint512 num(std::to_string(i));
        res = res && (num == base::Uint512(i));
    }
    BOOST_CHECK(res);

    /*str_num = "";
    for(uint32_t i = 0; i < 512; i++){
           str_num += '1';
    }
    base::Uint512 num512(str_num);
    BOOST_CHECK(num512 == base::Uint512(str_num));*/
}

//----------------------------------

BOOST_AUTO_TEST_CASE(BigNum_constructor_copy_move)
{
    base::Uint256 num1(123u);
    base::Uint256 num2(num1);
    base::Uint256 num21(std::move(num1));
    BOOST_CHECK(num1 == num2);
    BOOST_CHECK(num1 == num21);

    base::Uint256 num3(789ll);
    base::Uint256 num4(num3);
    base::Uint256 num41(std::move(num3));
    BOOST_CHECK(num3 == num4);
    BOOST_CHECK(num3 == num41);

    base::Uint256 num5("456123");
    base::Uint256 num6(num5);
    base::Uint256 num61(std::move(num5));
    BOOST_CHECK(num5 == num6);
    BOOST_CHECK(num5 == num61);


    base::Uint256 num12(456u);
    base::Uint256 num22(num12);
    base::Uint256 num212(std::move(num12));
    BOOST_CHECK(num12 == num22);
    BOOST_CHECK(num12 == num212);

    base::Uint256 num32(333333ll);
    base::Uint256 num42(num32);
    base::Uint256 num412(std::move(num32));
    BOOST_CHECK(num32 == num42);
    BOOST_CHECK(num32 == num412);

    base::Uint256 num52("7777777");
    base::Uint256 num62(num52);
    base::Uint256 num612(std::move(num52));
    BOOST_CHECK(num52 == num62);
    BOOST_CHECK(num52 == num612);
}

//----------------------------------

BOOST_AUTO_TEST_CASE(BigNum_operator_equal_copy_move)
{
    base::Uint256 num1(456u);
    base::Uint256 num2 = num1;
    BOOST_CHECK(num1 == num2);

    base::Uint256 num22(123u);
    num1 = std::move(num22);
    BOOST_CHECK(num1 == num22);

    base::Uint256 num3(789ll);
    num1 = num3;
    BOOST_CHECK(num1 == num3);

    base::Uint256 num33(456ll);
    num1 = std::move(num33);
    BOOST_CHECK(num1 == num33);

    base::Uint256 num4("123456");
    num1 = num4;
    BOOST_CHECK(num1 == num4);

    base::Uint256 num44("654987");
    num1 = std::move(num44);
    BOOST_CHECK(num1 == num44);


    base::Uint512 num11(456u);
    base::Uint512 num12 = num11;
    BOOST_CHECK(num11 == num12);

    base::Uint512 num222(123u);
    num11 = std::move(num222);
    BOOST_CHECK(num11 == num222);

    base::Uint512 num133(789ll);
    num11 = num133;
    BOOST_CHECK(num11 == num133);

    base::Uint512 num333(456ll);
    num11 = std::move(num333);
    BOOST_CHECK(num11 == num333);

    base::Uint512 num144("123456");
    num11 = num144;
    BOOST_CHECK(num11 == num144);

    base::Uint512 num444("654987");
    num11 = std::move(num444);
    BOOST_CHECK(num11 == num444);
}

//----------------------------------

BOOST_AUTO_TEST_CASE(BigNum_operator_plus_minus)
{
     bool res = true;
     base::Uint256 num1(123456789u);
     for(uint32_t i = 0; i < 1000; i++){
         base::Uint256 num = num1 + base::Uint256(i);
         res = res && (num == base::Uint256(123456789u + i));
     }
     BOOST_CHECK(res);

     res = true;
     for(uint32_t i = 10000; i < 11111; i++){
         base::Uint256 num = num1 - base::Uint256(i);
         res = res && (num == base::Uint256(123456789u - i));
     }
     BOOST_CHECK(res);


     res = true;
     base::Uint512 num2(123456789u);
     for(uint32_t i = 0; i < 1000; i++){
         base::Uint512 num = num2 + base::Uint512(i);
         res = res && (num == base::Uint512(123456789u + i));
     }
     BOOST_CHECK(res);

     res = true;
     for(uint32_t i = 10000; i < 11111; i++){
         base::Uint512 num = num2 - base::Uint512(i);
         res = res && (num == base::Uint512(123456789u - i));
     }
     BOOST_CHECK(res);
}

//----------------------------------

BOOST_AUTO_TEST_CASE(BigNum_operator_multiply_divide)
{
    bool res = true;
    base::Uint256 num1(123456789u);
    for(uint64_t i = 0; i < 1000; i++){
        base::Uint256 num = num1 * base::Uint256(i);
        res = res && (num == base::Uint256(123456789u * i));
    }
    BOOST_CHECK(res);

    res = true;
    for(uint64_t i = 10000; i < 11111; i++){
        base::Uint256 num = num1 / base::Uint256(i);
        res = res && (num == base::Uint256(123456789u / i));
    }
    BOOST_CHECK(res);


    res = true;
    base::Uint512 num2(123456789u);
    for(uint64_t i = 0; i < 1000; i++){
        base::Uint512 num = num2 * base::Uint512(i);
        res = res && (num == base::Uint512(123456789u * i));
    }
    BOOST_CHECK(res);

    res = true;
    for(uint64_t i = 10000; i < 11111; i++){
        base::Uint512 num = num2 / base::Uint512(i);
        res = res && (num == base::Uint512(123456789u / i));
    }
    BOOST_CHECK(res);
}

//----------------------------------

BOOST_AUTO_TEST_CASE(BigNum_operator_plus_minus_equal)
{
    bool res = true;
    base::Uint256 num1(123456789u);
    for(uint64_t i = 0; i < 1000; i++){
        base::Uint256 num = num1;
        num1 += base::Uint256(i);
        res = res && ((num + base::Uint256(i)) == num1);
    }
    BOOST_CHECK(res);

    res = true;
    for(uint64_t i = 10000; i < 11111; i++){
        base::Uint256 num = num1;
        num1 -= base::Uint256(i);
        res = res && ((num - base::Uint256(i)) == num1);
    }
    BOOST_CHECK(res);


    res = true;
    base::Uint512 num2(123456789u);
    for(uint64_t i = 0; i < 1000; i++){
        base::Uint512 num = num2;
        num2 += base::Uint512(i);
        res = res && ((num + base::Uint512(i)) == num2);
    }
    BOOST_CHECK(res);

    res = true;
    for(uint64_t i = 10000; i < 11111; i++){
        base::Uint512 num = num2;
        num2 -= base::Uint512(i);
        res = res && ((num - base::Uint512(i)) == num2);
    }
    BOOST_CHECK(res);
}

//----------------------------------

BOOST_AUTO_TEST_CASE(BigNum_operator_multiply_divide_equal)
{
    bool res = true;
    base::Uint256 num1(123456789u);
    for(uint64_t i = 0; i < 1000; i++){
        base::Uint256 num = num1;
        num1 *= base::Uint256(i);
        res = res && ((num * base::Uint256(i)) == num1);
    }
    BOOST_CHECK(res);

    res = true;
    for(uint64_t i = 10000; i < 11111; i++){
        base::Uint256 num = num1;
        num1 /= base::Uint256(i);
        res = res && ((num / base::Uint256(i)) == num1);
    }
    BOOST_CHECK(res);


    res = true;
    base::Uint512 num2(123456789u);
    for(uint64_t i = 0; i < 1000; i++){
        base::Uint512 num = num2;
        num2 *= base::Uint512(i);
        res = res && ((num * base::Uint512(i)) == num2);
    }
    BOOST_CHECK(res);

    res = true;
    for(uint64_t i = 10000; i < 11111; i++){
        base::Uint512 num = num2;
        num2 /= base::Uint512(i);
        res = res && ((num / base::Uint512(i)) == num2);
    }
    BOOST_CHECK(res);
}

//----------------------------------

BOOST_AUTO_TEST_CASE(BigNum_all_bool_operator)
{
    base::Uint256 num1(123456);
    BOOST_CHECK(num1 == base::Uint256(123456));
    BOOST_CHECK(num1 != base::Uint256(123457));
    BOOST_CHECK(num1 > base::Uint256(123455));
    BOOST_CHECK(num1 < base::Uint256(123457));
    BOOST_CHECK(num1 >= base::Uint256(123456));
    BOOST_CHECK(num1 >= base::Uint256(123455));
    BOOST_CHECK(num1 <= base::Uint256(123456));
    BOOST_CHECK(num1 <= base::Uint256(123457));

    base::Uint512 num2(123456);
    BOOST_CHECK(num2 == base::Uint512(123456));
    BOOST_CHECK(num2 != base::Uint512(123457));
    BOOST_CHECK(num2 > base::Uint512(123455));
    BOOST_CHECK(num2 < base::Uint512(123457));
    BOOST_CHECK(num2 >= base::Uint512(123456));
    BOOST_CHECK(num2 >= base::Uint512(123455));
    BOOST_CHECK(num2 <= base::Uint512(123456));
    BOOST_CHECK(num2 <= base::Uint512(123457));
}

//----------------------------------

BOOST_AUTO_TEST_CASE(BigNum_toString)
{
    base::Uint256 num1(999999);
    BOOST_CHECK(num1.toString() == "999999");
    BOOST_CHECK((num1 + base::Uint256(11).toString() == "1000010"));

    base::Uint256 num2("1111111");
    BOOST_CHECK(num2.toString() == "1111111");
    BOOST_CHECK((num2 + base::Uint256(88).toString() == "1111199"));

    base::Uint512 num11(999999);
    BOOST_CHECK(num11.toString() == "999999");
    BOOST_CHECK((num11 + base::Uint512(11).toString() == "1000010"));

    base::Uint512 num22("1111111");
    BOOST_CHECK(num22.toString() == "1111111");
    BOOST_CHECK((num22 + base::Uint512(88).toString() == "1111199"));
}

//----------------------------------

BOOST_AUTO_TEST_CASE(BigNum_operator_iostream)
{
    std::stringstream stream;
    base::Uint256 num1(777777);
    stream << num1 << ' ' <<  888;
    std::string str, str2;
    stream >> str >> str2;
    BOOST_CHECK(str == "777777" && str2 == "888");
}
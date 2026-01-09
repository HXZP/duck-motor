/*
    基于SVPWM的FOC算法实现

*/
#include "foc/foc_core.h"
#include <stdlib.h>
#include "app/log.h"

/*
    **  @brief  N值到扇区
*/
const uint8_t N2sector[6] = {2,6,1,4,3,5};
/*
    **  @brief  扇区到两个子向量 顺序为扇区的逆时针方向
*/
const uint8_t sector2vector[6][2] = {
    {4, 6},
    {6, 2},
    {2, 3},
    {3, 1},
    {1, 5},
	{5, 4}
};
/*
    **  @brief  从扇区到两个基向量的加权 顺序为扇区的逆时针方向
*/
//常见xy的向量方向：由小序号指向大序号
//const int8_t sector2vector_mask[6][2][3] = {
//    {{0,0,-1},{1,0,0}},
//    {{0,0,1},{0,1,0}},
//    {{1,0,0},{0,-1,0}},
//    {{-1,0,0},{0,0,1}},
//    {{0,-1,0},{0,0,-1}},
//    {{0,1,0},{-1,0,0}}
//};
//我的向量方向，按逆时针指向
const int8_t sector2vector_mask[6][2][3] = {
    {{0,0,-1},{1,0,0}},
    {{0,1,0},{0,0,1}},
    {{1,0,0},{0,-1,0}},
    {{0,0,1},{-1,0,0}},
    {{0,-1,0},{0,0,-1}},
    {{-1,0,0},{0,1,0}}
};


#define LUT_SIZE 1572
#define SIN_BIT 10
// Q10格式的sin查找表 (0-π/2范围)
static const int16_t sin_lut[LUT_SIZE] = {
     0,    1,    2,    3,    4,    5,    6,    7,    8,    9,   10,   11,   12,   13,   14,   15,
    16,   17,   18,   19,   20,   21,   23,   24,   25,   26,   27,   28,   29,   30,   31,   32,
    33,   34,   35,   36,   37,   38,   39,   40,   41,   42,   43,   44,   45,   46,   47,   48,
    49,   50,   51,   52,   53,   54,   55,   56,   57,   58,   59,   60,   61,   62,   63,   64,
    65,   67,   68,   69,   70,   71,   72,   73,   74,   75,   76,   77,   78,   79,   80,   81,
    82,   83,   84,   85,   86,   87,   88,   89,   90,   91,   92,   93,   94,   95,   96,   97,
    98,   99,  100,  101,  102,  103,  104,  105,  106,  107,  108,  109,  110,  111,  112,  113,
   114,  115,  116,  117,  119,  120,  121,  122,  123,  124,  125,  126,  127,  128,  129,  130,
   131,  132,  133,  134,  135,  136,  137,  138,  139,  140,  141,  142,  143,  144,  145,  146,
   147,  148,  149,  150,  151,  152,  153,  154,  155,  156,  157,  158,  159,  160,  161,  162,
   163,  164,  165,  166,  167,  168,  169,  170,  171,  172,  173,  174,  175,  176,  177,  178,
   179,  180,  181,  182,  183,  184,  185,  186,  187,  188,  189,  190,  191,  192,  193,  194,
   195,  196,  197,  198,  199,  200,  201,  202,  203,  204,  205,  206,  207,  208,  209,  210,
   211,  212,  213,  214,  215,  216,  217,  218,  219,  220,  221,  222,  223,  224,  225,  226,
   227,  228,  229,  230,  231,  232,  233,  234,  235,  236,  237,  238,  239,  240,  241,  242,
   243,  244,  245,  246,  247,  248,  249,  250,  251,  252,  253,  254,  255,  256,  257,  258,
   259,  260,  261,  262,  263,  264,  265,  266,  267,  268,  269,  270,  271,  272,  273,  274,
   275,  276,  277,  278,  279,  280,  281,  282,  283,  284,  285,  286,  287,  288,  289,  290,
   291,  292,  293,  294,  295,  296,  297,  298,  299,  300,  301,  302,  303,  304,  305,  306,
   306,  307,  308,  309,  310,  311,  312,  313,  314,  315,  316,  317,  318,  319,  320,  321,
   322,  323,  324,  325,  326,  327,  328,  329,  330,  331,  332,  333,  334,  335,  336,  337,
   338,  339,  340,  340,  341,  342,  343,  344,  345,  346,  347,  348,  349,  350,  351,  352,
   353,  354,  355,  356,  357,  358,  359,  360,  361,  362,  363,  364,  365,  365,  366,  367,
   368,  369,  370,  371,  372,  373,  374,  375,  376,  377,  378,  379,  380,  381,  382,  383,
   384,  385,  385,  386,  387,  388,  389,  390,  391,  392,  393,  394,  395,  396,  397,  398,
   399,  400,  401,  402,  402,  403,  404,  405,  406,  407,  408,  409,  410,  411,  412,  413,
   414,  415,  416,  417,  417,  418,  419,  420,  421,  422,  423,  424,  425,  426,  427,  428,
   429,  430,  431,  431,  432,  433,  434,  435,  436,  437,  438,  439,  440,  441,  442,  443,
   444,  444,  445,  446,  447,  448,  449,  450,  451,  452,  453,  454,  455,  455,  456,  457,
   458,  459,  460,  461,  462,  463,  464,  465,  466,  466,  467,  468,  469,  470,  471,  472,
   473,  474,  475,  476,  476,  477,  478,  479,  480,  481,  482,  483,  484,  485,  485,  486,
   487,  488,  489,  490,  491,  492,  493,  494,  494,  495,  496,  497,  498,  499,  500,  501,
   502,  503,  503,  504,  505,  506,  507,  508,  509,  510,  511,  511,  512,  513,  514,  515,
   516,  517,  518,  518,  519,  520,  521,  522,  523,  524,  525,  526,  526,  527,  528,  529,
   530,  531,  532,  533,  533,  534,  535,  536,  537,  538,  539,  540,  540,  541,  542,  543,
   544,  545,  546,  546,  547,  548,  549,  550,  551,  552,  553,  553,  554,  555,  556,  557,
   558,  559,  559,  560,  561,  562,  563,  564,  565,  565,  566,  567,  568,  569,  570,  570,
   571,  572,  573,  574,  575,  576,  576,  577,  578,  579,  580,  581,  582,  582,  583,  584,
   585,  586,  587,  587,  588,  589,  590,  591,  592,  592,  593,  594,  595,  596,  597,  597,
   598,  599,  600,  601,  602,  602,  603,  604,  605,  606,  607,  607,  608,  609,  610,  611,
   611,  612,  613,  614,  615,  616,  616,  617,  618,  619,  620,  620,  621,  622,  623,  624,
   625,  625,  626,  627,  628,  629,  629,  630,  631,  632,  633,  633,  634,  635,  636,  637,
   637,  638,  639,  640,  641,  641,  642,  643,  644,  645,  645,  646,  647,  648,  649,  649,
   650,  651,  652,  653,  653,  654,  655,  656,  656,  657,  658,  659,  660,  660,  661,  662,
   663,  664,  664,  665,  666,  667,  667,  668,  669,  670,  671,  671,  672,  673,  674,  674,
   675,  676,  677,  677,  678,  679,  680,  681,  681,  682,  683,  684,  684,  685,  686,  687,
   687,  688,  689,  690,  690,  691,  692,  693,  693,  694,  695,  696,  696,  697,  698,  699,
   699,  700,  701,  702,  702,  703,  704,  705,  705,  706,  707,  708,  708,  709,  710,  711,
   711,  712,  713,  714,  714,  715,  716,  716,  717,  718,  719,  719,  720,  721,  722,  722,
   723,  724,  724,  725,  726,  727,  727,  728,  729,  729,  730,  731,  732,  732,  733,  734,
   734,  735,  736,  737,  737,  738,  739,  739,  740,  741,  742,  742,  743,  744,  744,  745,
   746,  747,  747,  748,  749,  749,  750,  751,  751,  752,  753,  753,  754,  755,  756,  756,
   757,  758,  758,  759,  760,  760,  761,  762,  762,  763,  764,  764,  765,  766,  767,  767,
   768,  769,  769,  770,  771,  771,  772,  773,  773,  774,  775,  775,  776,  777,  777,  778,
   779,  779,  780,  781,  781,  782,  783,  783,  784,  785,  785,  786,  787,  787,  788,  789,
   789,  790,  790,  791,  792,  792,  793,  794,  794,  795,  796,  796,  797,  798,  798,  799,
   799,  800,  801,  801,  802,  803,  803,  804,  805,  805,  806,  806,  807,  808,  808,  809,
   810,  810,  811,  812,  812,  813,  813,  814,  815,  815,  816,  816,  817,  818,  818,  819,
   820,  820,  821,  821,  822,  823,  823,  824,  824,  825,  826,  826,  827,  827,  828,  829,
   829,  830,  830,  831,  832,  832,  833,  833,  834,  835,  835,  836,  836,  837,  838,  838,
   839,  839,  840,  841,  841,  842,  842,  843,  843,  844,  845,  845,  846,  846,  847,  847,
   848,  849,  849,  850,  850,  851,  851,  852,  853,  853,  854,  854,  855,  855,  856,  857,
   857,  858,  858,  859,  859,  860,  860,  861,  862,  862,  863,  863,  864,  864,  865,  865,
   866,  867,  867,  868,  868,  869,  869,  870,  870,  871,  871,  872,  872,  873,  874,  874,
   875,  875,  876,  876,  877,  877,  878,  878,  879,  879,  880,  880,  881,  881,  882,  883,
   883,  884,  884,  885,  885,  886,  886,  887,  887,  888,  888,  889,  889,  890,  890,  891,
   891,  892,  892,  893,  893,  894,  894,  895,  895,  896,  896,  897,  897,  898,  898,  899,
   899,  900,  900,  901,  901,  902,  902,  903,  903,  904,  904,  905,  905,  905,  906,  906,
   907,  907,  908,  908,  909,  909,  910,  910,  911,  911,  912,  912,  913,  913,  913,  914,
   914,  915,  915,  916,  916,  917,  917,  918,  918,  918,  919,  919,  920,  920,  921,  921,
   922,  922,  923,  923,  923,  924,  924,  925,  925,  926,  926,  926,  927,  927,  928,  928,
   929,  929,  930,  930,  930,  931,  931,  932,  932,  933,  933,  933,  934,  934,  935,  935,
   935,  936,  936,  937,  937,  938,  938,  938,  939,  939,  940,  940,  940,  941,  941,  942,
   942,  942,  943,  943,  944,  944,  944,  945,  945,  946,  946,  946,  947,  947,  948,  948,
   948,  949,  949,  949,  950,  950,  951,  951,  951,  952,  952,  952,  953,  953,  954,  954,
   954,  955,  955,  955,  956,  956,  957,  957,  957,  958,  958,  958,  959,  959,  959,  960,
   960,  961,  961,  961,  962,  962,  962,  963,  963,  963,  964,  964,  964,  965,  965,  965,
   966,  966,  966,  967,  967,  967,  968,  968,  968,  969,  969,  969,  970,  970,  970,  971,
   971,  971,  972,  972,  972,  973,  973,  973,  974,  974,  974,  975,  975,  975,  976,  976,
   976,  976,  977,  977,  977,  978,  978,  978,  979,  979,  979,  979,  980,  980,  980,  981,
   981,  981,  982,  982,  982,  982,  983,  983,  983,  984,  984,  984,  984,  985,  985,  985,
   986,  986,  986,  986,  987,  987,  987,  987,  988,  988,  988,  989,  989,  989,  989,  990,
   990,  990,  990,  991,  991,  991,  991,  992,  992,  992,  992,  993,  993,  993,  993,  994,
   994,  994,  994,  995,  995,  995,  995,  996,  996,  996,  996,  997,  997,  997,  997,  998,
   998,  998,  998,  998,  999,  999,  999,  999, 1000, 1000, 1000, 1000, 1000, 1001, 1001, 1001,
  1001, 1002, 1002, 1002, 1002, 1002, 1003, 1003, 1003, 1003, 1003, 1004, 1004, 1004, 1004, 1004,
  1005, 1005, 1005, 1005, 1005, 1006, 1006, 1006, 1006, 1006, 1007, 1007, 1007, 1007, 1007, 1007,
  1008, 1008, 1008, 1008, 1008, 1009, 1009, 1009, 1009, 1009, 1009, 1010, 1010, 1010, 1010, 1010,
  1010, 1011, 1011, 1011, 1011, 1011, 1011, 1012, 1012, 1012, 1012, 1012, 1012, 1013, 1013, 1013,
  1013, 1013, 1013, 1013, 1014, 1014, 1014, 1014, 1014, 1014, 1014, 1015, 1015, 1015, 1015, 1015,
  1015, 1015, 1015, 1016, 1016, 1016, 1016, 1016, 1016, 1016, 1017, 1017, 1017, 1017, 1017, 1017,
  1017, 1017, 1017, 1018, 1018, 1018, 1018, 1018, 1018, 1018, 1018, 1018, 1019, 1019, 1019, 1019,
  1019, 1019, 1019, 1019, 1019, 1019, 1020, 1020, 1020, 1020, 1020, 1020, 1020, 1020, 1020, 1020,
  1020, 1021, 1021, 1021, 1021, 1021, 1021, 1021, 1021, 1021, 1021, 1021, 1021, 1021, 1022, 1022,
  1022, 1022, 1022, 1022, 1022, 1022, 1022, 1022, 1022, 1022, 1022, 1022, 1022, 1023, 1023, 1023,
  1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023,
  1023, 1023, 1023, 1023, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024,
  1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024,
  1024, 1024, 1024, 1024
};

// 角度常量定义 (弧度 × 1000)
#define PI_X1000      3141   // π × 1000
#define PI_HALF_X1000 1570   // π/2 × 1000  
#define PI_3HALF_X1000 4712  // 3π/2 × 1000
#define TWOPI_X1000   6283   // 2π × 1000 6,288

// 角度归一化到 [0, 6283) 范围
static int32_t angle_normalize(int32_t angle_x1000) {
    int32_t angle = angle_x1000;
    
    // 处理负角度
    while (angle < 0) {
        angle += TWOPI_X1000;
    }
    
    // 处理大于2π的角度
    angle %= TWOPI_X1000;
    
    return angle;
}

// 计算sin值，输入角度×1000，输出sin×1024
int32_t sin_int16(int32_t angle_x1000) {
    int32_t angle = angle_normalize(angle_x1000);
    int32_t result;
    
    // 根据象限处理
    if (angle < PI_HALF_X1000) {
        // 第一象限：直接查表
        result = sin_lut[angle];
    } else if (angle < PI_X1000) {
        // 第二象限：sin(π - θ) = sin(θ)
        result = sin_lut[PI_X1000 - angle];
    } else if (angle < PI_3HALF_X1000) {
        // 第三象限：sin(π + θ) = -sin(θ)
        result = -sin_lut[angle - PI_X1000];
    } else {
        // 第四象限：sin(2π - θ) = -sin(θ)
        result = -sin_lut[TWOPI_X1000 - angle];
    }
    
    return result;
}

// 计算cos值，利用cos(θ) = sin(θ + π/2)
int32_t cos_int16(int32_t angle_x1000) {
    // cos(θ) = sin(θ + π/2)
    int32_t new_angle = (int32_t)angle_x1000 + PI_HALF_X1000;
    return sin_int16(new_angle % TWOPI_X1000);
}


/*
    **  @brief  基本模型
*/
/*
    **  @brief  Clarke变换
                a-b-c和α-β的关系
*/
void foc_clarke(foc_clarke_t *clarke, const foc_phase_volage_t *phase_volage)
{
    clarke->alpha = ((phase_volage->a - phase_volage->b - phase_volage->c)>>1)/3;
    clarke->beta = (phase_volage->b - phase_volage->c)*INF_SQRT_3;
}
/*
    **  @brief  Park变换
                α-β和d-q的关系
*/
void foc_park(foc_park_t *park, const foc_clarke_t *clarke)
{
    park->d = (clarke->alpha * cos_int16(park->theta) + clarke->beta * sin_int16(park->theta))>>SIN_BIT;
    park->q = (-clarke->alpha * sin_int16(park->theta) + clarke->beta * cos_int16(park->theta))>>SIN_BIT;
}
/*
    **  @brief  反Clarke变换
                α-β和a-b-c的关系
*/
void foc_reclarke(foc_phase_volage_t *phase_volage, const foc_clarke_t *clarke)
{
    phase_volage->a = clarke->alpha;
    phase_volage->b = (-clarke->alpha + clarke->beta * SQRT_3)>>1;
    phase_volage->c = (-clarke->alpha - clarke->beta * SQRT_3)>>1;
}
/*
    **  @brief  反Park变换
                d-q和α-β的关系
*/
int32_t sin_out;
int32_t cos_out;
void foc_repark(foc_clarke_t *clarke, const foc_park_t *park)
{
    cos_out = cos_int16(park->theta);
    sin_out = sin_int16(park->theta);
    clarke->alpha = (park->d * cos_int16(park->theta) - park->q * sin_int16(park->theta))>>SIN_BIT;
    clarke->beta = (park->d * sin_int16(park->theta) + park->q * cos_int16(park->theta))>>SIN_BIT;
}

/*
    **  @brief  扇区判断,根据向量落点对于三根直线的关系获取扇区,本质上的是判断向量落在哪个象限
*/
void foc_get_sector(foc_sector_t *sector, const foc_clarke_t *clarke)
{
	sector->A = (clarke->beta > 0)?1:0;
	sector->B = (SQRT_3 * clarke->alpha - clarke->beta > 0)?1:0;
	sector->C = (-SQRT_3 * clarke->alpha - clarke->beta > 0)?1:0;
	
    sector->N = (sector->C << 2) + (sector->B << 1) + sector->A;
    sector->value = N2sector[sector->N - 1];
}

void foc_get_vector_percent(foc_vector_percent_t *vector_percent, const float vector_voltage, const foc_clarke_t *clarke)
{
    vector_percent->X = clarke->beta;
    vector_percent->Y = (clarke->alpha * SQRT_3 + clarke->beta)>>1;
    vector_percent->Z = (-clarke->alpha * SQRT_3 + clarke->beta)>>1;
}

void foc_get_vector_normalize(foc_output_vector_t *vector, const foc_vector_percent_t *vector_percent, const foc_sector_t *sector)
{
    int32_t percent_x = 0;
    int32_t percent_y = 0;

    percent_x = sector2vector_mask[sector->value - 1][0][0] * vector_percent->X
                + sector2vector_mask[sector->value - 1][0][1] * vector_percent->Y
                  + sector2vector_mask[sector->value - 1][0][2] * vector_percent->Z;

    percent_y = sector2vector_mask[sector->value - 1][1][0] * vector_percent->X
                + sector2vector_mask[sector->value - 1][1][1] * vector_percent->Y
                  + sector2vector_mask[sector->value - 1][1][2] * vector_percent->Z;

    if(percent_x + percent_y > OUT_MAX)
    {
        vector->vector_x.percent = (percent_x * OUT_MAX)/(percent_x + percent_y);
        vector->vector_y.percent = (percent_y * OUT_MAX)/(percent_x + percent_y);
    }                
    else
    {
        vector->vector_x.percent = percent_x;
        vector->vector_y.percent = percent_y;
    }
		
    if(percent_x + percent_y == 0)
    {
        vector->vector_0.percent = 0;
        vector->vector_7.percent = 0;		
    }
    else
    {
        vector->vector_0.percent = (OUT_MAX - percent_x - percent_y)>>1;
        vector->vector_7.percent = (OUT_MAX - percent_x - percent_y)>>1;		
    }

    vector->vector_x.direction.value = sector2vector[sector->value - 1][0];
    vector->vector_y.direction.value = sector2vector[sector->value - 1][1];
    vector->vector_0.direction.value = 0;
    vector->vector_7.direction.value = 7;
}

void foc_get_vector_strength(foc_output_vector_t *vector, const float vector_voltage)
{
    vector->vector_x.strength = vector->vector_x.percent;
    vector->vector_y.strength = vector->vector_y.percent;
    vector->vector_0.strength = vector->vector_0.percent;
    vector->vector_7.strength = vector->vector_7.percent;
}

void foc_get_pwm_duty(foc_pwm_duty_t *pwm, const foc_output_vector_t *vector)
{
    uint32_t pwm_x = 0;
    uint32_t pwm_y = 0;
    uint32_t pwm_0 = 0;	
    uint32_t pwm_7 = 0;		
    
    pwm_x = (vector->vector_x.percent * pwm->T)>>OUT_MAX_BIT;
    pwm_y = (vector->vector_y.percent * pwm->T)>>OUT_MAX_BIT;
    pwm_0 = (vector->vector_0.percent * pwm->T)>>OUT_MAX_BIT;
    pwm_7 = (vector->vector_7.percent * pwm->T)>>OUT_MAX_BIT;

	pwm->a = pwm_x * vector->vector_x.direction.half_bridge.Sa
            + pwm_y * vector->vector_y.direction.half_bridge.Sa
            + pwm_0 * vector->vector_0.direction.half_bridge.Sa
            + pwm_7 * vector->vector_7.direction.half_bridge.Sa;

	pwm->b = pwm_x * vector->vector_x.direction.half_bridge.Sb
            + pwm_y * vector->vector_y.direction.half_bridge.Sb
            + pwm_0 * vector->vector_0.direction.half_bridge.Sb
            + pwm_7 * vector->vector_7.direction.half_bridge.Sb;     
	
    pwm->c = pwm_x * vector->vector_x.direction.half_bridge.Sc
            + pwm_y * vector->vector_y.direction.half_bridge.Sc
            + pwm_0 * vector->vector_0.direction.half_bridge.Sc
            + pwm_7 * vector->vector_7.direction.half_bridge.Sc;        

}


int32_t foc_angle_cycle(int32_t angle)
{
    if(angle > TWOPI_X1000)
    {
        return angle - TWOPI_X1000;
    }
    else if(angle < 0)
    {
        return angle + TWOPI_X1000;
    }
    else
    {
        return angle;
    }
}


void foc_init(foc_t *foc, const foc_cfg_t *cfg)
{
    foc->info.master_voltage = cfg->master_voltage;
    foc->info.vector_voltage = OUT_MAX;//foc->info.master_voltage/SQRT_3;
	
    foc->info.pole_pairs = cfg->pole_pairs;
    foc->pwm_duty.T = cfg->pwm_period;
	
	foc->cfg = cfg;
}

/*angle:0~1*/
int32_t foc_sensor_updata(foc_t *foc)
{  
    foc->angle.sensor_angle = foc->cfg->get_angle_rad();
    foc->angle.mech_angle = foc_angle_cycle(foc->angle.sensor_angle - foc->angle.zero_angle);
    foc->angle.elec_angle = foc->angle.mech_angle * foc->info.pole_pairs;
	foc->park.theta = foc->angle.elec_angle;//angle_normalize(foc->angle.elec_angle);

//    foc->speed.last_time = foc->speed.now_time;
//    foc->speed.now_time = foc->cfg->get_time();
//    foc->speed.time_diff = foc->speed.now_time - foc->speed.last_time;
    
    foc->speed.last_mech_angle = foc->speed.mech_angle;
    foc->speed.mech_angle = foc->angle.mech_angle;
    
    int32_t angle_err;
    angle_err = (foc->angle.mech_angle - foc->speed.last_mech_angle);
    if(angle_err > FOC_PIx1000)
    {
        angle_err -= 2*FOC_PIx1000;
    }
    else if(angle_err < -FOC_PIx1000)
    {
        angle_err += 2*FOC_PIx1000;
    }
    foc->speed.mech_angle_diff = angle_err;
    
    foc->angle.angle = foc->angle.mech_angle;
    foc->speed.speed = foc->speed.mech_angle_diff;
    
//    printf("mech_angle: %d, mech_angle_diff: %d\n", foc->speed.mech_angle, foc->speed.mech_angle_diff);
    return foc->angle.sensor_angle;
}

int32_t foc_get_angle(foc_t *foc)
{
    return foc->angle.angle;
}

int32_t foc_get_speed(foc_t *foc)
{
    return foc->speed.speed;
}

void foc_target_updata(foc_t *foc, foc_park_t target)
{
    // if(target->d * target->d + target->q * target->q > foc->info.vector_voltage * foc->info.vector_voltage)
    // {
    //     // 如果目标电压的平方和大于最大电压的平方和，则将目标电压限制在最大电压范围内
    // }

    foc->target_park = target;


}


void foc_control(foc_t *foc)
{
    if(foc->state != Foc_Working)
    {
        return;
    }
    
    foc->park.d = foc->target_park.d;
    foc->park.q = foc->target_park.q;

    foc_repark(&foc->clarke, &foc->park);
	
    foc_get_sector(&foc->sector, &foc->clarke);
    foc_get_vector_percent(&foc->vector_percent, foc->info.vector_voltage, &foc->clarke);
    foc_get_vector_normalize(&foc->output_vector, &foc->vector_percent, &foc->sector);
    foc_get_pwm_duty(&foc->pwm_duty, &foc->output_vector);
    
    foc->cfg->output(foc->pwm_duty.a,foc->pwm_duty.b,foc->pwm_duty.c);
    
//    foc->solving.cnt++;
//    if(foc->solving.cnt >= foc->cfg->control_khz)
//    {
//        foc->solving.update_flag = 1;
//        foc->solving.cnt = 0;
//    }
}

int32_t foc_zero_reset(foc_t *foc)
{
    if(foc->state == Foc_Zero_Angle_Init)
    {
        foc->cfg->output(foc->cfg->pwm_period*90/100,0,0);
        foc->cfg->delay(300);
        foc->angle.zero_angle = foc->cfg->get_angle_rad();
        foc->cfg->output(0,0,0);
        foc->state = Foc_Working;
    }

    return foc->angle.zero_angle;
}

void foc_zero_reset_manual(foc_t *foc, int32_t angle)
{
    if(foc->state == Foc_Zero_Angle_Init)
    {
        foc->angle.zero_angle = angle;
        foc->state = Foc_Working;
    }
}


uint8_t foc_get_angle_update_flag(foc_t *foc)
{
    if(foc->solving.update_flag)
    {
        foc->solving.update_flag = 0;
        return 1;
    }
    
    return 0;
}

/* AMD zn2/32 gmp-mparam.h -- Compiler/machine parameter header file.

Copyright 2019 Free Software Foundation, Inc.

This file is part of the GNU MP Library.

The GNU MP Library is free software; you can redistribute it and/or modify
it under the terms of either:

  * the GNU Lesser General Public License as published by the Free
    Software Foundation; either version 3 of the License, or (at your
    option) any later version.

or

  * the GNU General Public License as published by the Free Software
    Foundation; either version 2 of the License, or (at your option) any
    later version.

or both in parallel, as here.

The GNU MP Library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received copies of the GNU General Public License and the
GNU Lesser General Public License along with the GNU MP Library.  If not,
see https://www.gnu.org/licenses/.  */

#define GMP_LIMB_BITS 32
#define GMP_LIMB_BYTES 4

/* 3600-4400 MHz Matisse */
/* FFT tuning limit = 67,000,000 */
/* Generated by tuneup.c, 2019-10-23, gcc 8.3 */

#define MOD_1_NORM_THRESHOLD                 0  /* always */
#define MOD_1_UNNORM_THRESHOLD               0  /* always */
#define MOD_1N_TO_MOD_1_1_THRESHOLD          3
#define MOD_1U_TO_MOD_1_1_THRESHOLD          3
#define MOD_1_1_TO_MOD_1_2_THRESHOLD        15
#define MOD_1_2_TO_MOD_1_4_THRESHOLD         0  /* never mpn_mod_1s_2p */
#define PREINV_MOD_1_TO_MOD_1_THRESHOLD      9
#define USE_PREINV_DIVREM_1                  1  /* native */
#define DIV_QR_1N_PI1_METHOD                 1  /* 4.78% faster than 2 */
#define DIV_QR_1_NORM_THRESHOLD              3
#define DIV_QR_1_UNNORM_THRESHOLD        MP_SIZE_T_MAX  /* never */
#define DIV_QR_2_PI2_THRESHOLD               7
#define DIVEXACT_1_THRESHOLD                 0  /* always (native) */
#define BMOD_1_TO_MOD_1_THRESHOLD           23

#define DIV_1_VS_MUL_1_PERCENT             274

#define MUL_TOOM22_THRESHOLD                24
#define MUL_TOOM33_THRESHOLD                85
#define MUL_TOOM44_THRESHOLD               166
#define MUL_TOOM6H_THRESHOLD               290
#define MUL_TOOM8H_THRESHOLD               430

#define MUL_TOOM32_TO_TOOM43_THRESHOLD      97
#define MUL_TOOM32_TO_TOOM53_THRESHOLD     114
#define MUL_TOOM42_TO_TOOM53_THRESHOLD      97
#define MUL_TOOM42_TO_TOOM63_THRESHOLD     113
#define MUL_TOOM43_TO_TOOM54_THRESHOLD     130

#define SQR_BASECASE_THRESHOLD               0  /* always (native) */
#define SQR_TOOM2_THRESHOLD                 26
#define SQR_TOOM3_THRESHOLD                153
#define SQR_TOOM4_THRESHOLD                214
#define SQR_TOOM6_THRESHOLD                318
#define SQR_TOOM8_THRESHOLD                478

#define MULMID_TOOM42_THRESHOLD             48

#define MULMOD_BNM1_THRESHOLD               18
#define SQRMOD_BNM1_THRESHOLD               24

#define MUL_FFT_MODF_THRESHOLD             444  /* k = 5 */
#define MUL_FFT_TABLE3                                      \
  { {    444, 5}, {     21, 6}, {     11, 5}, {     23, 6}, \
    {     12, 5}, {     25, 6}, {     13, 5}, {     27, 6}, \
    {     25, 7}, {     13, 6}, {     27, 7}, {     15, 6}, \
    {     31, 7}, {     17, 6}, {     35, 7}, {     19, 6}, \
    {     39, 7}, {     27, 8}, {     15, 7}, {     35, 8}, \
    {     19, 7}, {     41, 8}, {     23, 7}, {     47, 8}, \
    {     27, 9}, {     15, 8}, {     31, 7}, {     63, 8}, \
    {     39, 9}, {     23, 8}, {     51,10}, {     15, 9}, \
    {     31, 8}, {     67, 9}, {     39, 8}, {     79, 9}, \
    {     47,10}, {     31, 9}, {     79,10}, {     47, 9}, \
    {     95,11}, {     31,10}, {     63, 9}, {    127,10}, \
    {     79, 9}, {    159,10}, {     95,11}, {     63,10}, \
    {    127, 9}, {    255, 8}, {    511,10}, {    143, 9}, \
    {    287, 8}, {    575,10}, {    159,11}, {     95,12}, \
    {     63,11}, {    127,10}, {    255, 9}, {    511,10}, \
    {    271, 9}, {    543,10}, {    287, 9}, {    575,11}, \
    {    159,10}, {    319, 9}, {    639,10}, {    335, 9}, \
    {    671, 8}, {   1343,10}, {    351, 9}, {    703,10}, \
    {    367, 9}, {    735,11}, {    191,10}, {    383, 9}, \
    {    767,10}, {    415,11}, {    223,10}, {    447,12}, \
    {    127,11}, {    255,10}, {    543, 9}, {   1087,11}, \
    {    287,10}, {    607,11}, {    319,10}, {    671, 9}, \
    {   1343,11}, {    351,10}, {    735,12}, {    191,11}, \
    {    383,10}, {    767,11}, {    415,10}, {    831,11}, \
    {    447,13}, {    127,12}, {    255,11}, {    543,10}, \
    {   1087,11}, {    607,10}, {   1215,12}, {    319,11}, \
    {    671,10}, {   1343,11}, {    735,10}, {   1471, 9}, \
    {   2943,12}, {    383,11}, {    799,10}, {   1599,11}, \
    {    863,12}, {    447,11}, {    959,10}, {   1919,13}, \
    {    255,12}, {    511,11}, {   1087,12}, {    575,11}, \
    {   1215,10}, {   2431,12}, {    639,11}, {   1343,12}, \
    {    703,11}, {   1471,10}, {   2943,13}, {    383,12}, \
    {    767,11}, {   1599,12}, {    831,11}, {   1727,10}, \
    {   3455,12}, {    959,11}, {   1919,10}, {   3839,14}, \
    {    255,13}, {    511,12}, {   1215,11}, {   2431,13}, \
    {    639,12}, {   1471,11}, {   2943,10}, {   5887,13}, \
    {    767,12}, {   1727,11}, {   3455,13}, {    895,12}, \
    {   1919,11}, {   3839,14}, {    511,13}, {   1023,12}, \
    {   2111,13}, {   1151,12}, {   2431,13}, {   1407,12}, \
    {   2943,11}, {   5887,14}, {    767,13}, {   1663,12}, \
    {   3455,13}, {   1919,12}, {   3839,15}, {    511,14}, \
    {   1023,13}, {   2431,14}, {   1279,13}, {   2943,12}, \
    {   5887,14}, {   1535,13}, {   3455,14}, {   1791,13}, \
    {   3839,12}, {   7679,13}, {   3967,12}, {   7935,11}, \
    {  15871,15}, {   1023,14}, {   2047,13}, {   4351,14}, \
    {   2303,13}, {   4991,12}, {   9983,14}, {   2815,13}, \
    {   5887,15}, {   1535,14}, {   3839,13}, {   7935,12}, \
    {  15871,16} }
#define MUL_FFT_TABLE3_SIZE 189
#define MUL_FFT_THRESHOLD                 4736

#define SQR_FFT_MODF_THRESHOLD             404  /* k = 5 */
#define SQR_FFT_TABLE3                                      \
  { {    404, 5}, {     21, 6}, {     11, 5}, {     23, 6}, \
    {     12, 5}, {     25, 6}, {     13, 5}, {     27, 6}, \
    {     25, 7}, {     13, 6}, {     27, 7}, {     15, 6}, \
    {     31, 7}, {     19, 6}, {     39, 7}, {     23, 6}, \
    {     47, 7}, {     27, 8}, {     15, 7}, {     35, 8}, \
    {     19, 7}, {     41, 8}, {     23, 7}, {     47, 8}, \
    {     27, 9}, {     15, 8}, {     39, 9}, {     23, 8}, \
    {     47,10}, {     15, 9}, {     31, 8}, {     63, 9}, \
    {     39, 8}, {     79, 9}, {     47,10}, {     31, 9}, \
    {     79,10}, {     47,11}, {     31,10}, {     63, 9}, \
    {    127,10}, {     95,11}, {     63,10}, {    127, 9}, \
    {    255, 8}, {    511, 9}, {    271,10}, {    143, 9}, \
    {    287, 8}, {    607, 7}, {   1215,11}, {     95,12}, \
    {     63,11}, {    127,10}, {    255, 9}, {    511,10}, \
    {    271, 9}, {    543, 8}, {   1087, 9}, {    607, 8}, \
    {   1215,11}, {    159, 9}, {    671, 8}, {   1343,10}, \
    {    351, 9}, {    735, 8}, {   1471,11}, {    191,10}, \
    {    383, 9}, {    767,10}, {    415,11}, {    223,12}, \
    {    127,11}, {    255,10}, {    543, 9}, {   1087,10}, \
    {    607, 9}, {   1215, 8}, {   2431,10}, {    671, 9}, \
    {   1343,10}, {    735, 9}, {   1471,12}, {    191,11}, \
    {    383,10}, {    767,11}, {    415,10}, {    831,13}, \
    {    127,12}, {    255,11}, {    543,10}, {   1087,11}, \
    {    607,10}, {   1215, 9}, {   2431,11}, {    671,10}, \
    {   1343,11}, {    735,10}, {   1471, 9}, {   2943,12}, \
    {    383,11}, {    863,12}, {    447,11}, {    959,10}, \
    {   1919,13}, {    255,12}, {    511,11}, {   1087,12}, \
    {    575,11}, {   1215,10}, {   2431,12}, {    639,11}, \
    {   1343,12}, {    703,11}, {   1471,10}, {   2943, 9}, \
    {   5887,12}, {    767,11}, {   1599,12}, {    831,11}, \
    {   1727,12}, {    959,11}, {   1919,10}, {   3839,14}, \
    {    255,13}, {    511,12}, {   1215,11}, {   2431,13}, \
    {    639,12}, {   1471,11}, {   2943,10}, {   5887,13}, \
    {    767,12}, {   1727,13}, {    895,12}, {   1919,11}, \
    {   3839,14}, {    511,13}, {   1023,12}, {   2111,13}, \
    {   1151,12}, {   2431,13}, {   1279,12}, {   2623,13}, \
    {   1407,12}, {   2943,11}, {   5887,14}, {    767,13}, \
    {   1663,12}, {   3455,13}, {   1919,12}, {   3839,15}, \
    {    511,14}, {   1023,13}, {   2431,14}, {   1279,13}, \
    {   2943,12}, {   5887,14}, {   1535,13}, {   3455,14}, \
    {   1791,13}, {   3839,12}, {   7679,13}, {   3967,12}, \
    {   7935,11}, {  15871,15}, {   1023,14}, {   2047,13}, \
    {   4223,14}, {   2303,13}, {   4991,12}, {   9983,14}, \
    {   2815,13}, {   5887,15}, {   1535,14}, {   3839,13}, \
    {   7935,12}, {  15871,16} }
#define SQR_FFT_TABLE3_SIZE 178
#define SQR_FFT_THRESHOLD                 3712

#define MULLO_BASECASE_THRESHOLD             4
#define MULLO_DC_THRESHOLD                  62
#define MULLO_MUL_N_THRESHOLD             8907
#define SQRLO_BASECASE_THRESHOLD             8
#define SQRLO_DC_THRESHOLD                 107
#define SQRLO_SQR_THRESHOLD               6633

#define DC_DIV_QR_THRESHOLD                 54
#define DC_DIVAPPR_Q_THRESHOLD             206
#define DC_BDIV_QR_THRESHOLD                55
#define DC_BDIV_Q_THRESHOLD                136

#define INV_MULMOD_BNM1_THRESHOLD           74
#define INV_NEWTON_THRESHOLD               212
#define INV_APPR_THRESHOLD                 204

#define BINV_NEWTON_THRESHOLD              292
#define REDC_1_TO_REDC_N_THRESHOLD          67

#define MU_DIV_QR_THRESHOLD               1442
#define MU_DIVAPPR_Q_THRESHOLD            1528
#define MUPI_DIV_QR_THRESHOLD               97
#define MU_BDIV_QR_THRESHOLD              1142
#define MU_BDIV_Q_THRESHOLD               1470

#define POWM_SEC_TABLE  1,16,96,386,1555

#define GET_STR_DC_THRESHOLD                10
#define GET_STR_PRECOMPUTE_THRESHOLD        16
#define SET_STR_DC_THRESHOLD               303
#define SET_STR_PRECOMPUTE_THRESHOLD       748

#define FAC_DSC_THRESHOLD                  141
#define FAC_ODD_THRESHOLD                   55

#define MATRIX22_STRASSEN_THRESHOLD         20
#define HGCD2_DIV1_METHOD                    1  /* 14.03% faster than 3 */
#define HGCD_THRESHOLD                     103
#define HGCD_APPR_THRESHOLD                127
#define HGCD_REDUCE_THRESHOLD             3014
#define GCD_DC_THRESHOLD                   396
#define GCDEXT_DC_THRESHOLD                265
#define JACOBI_BASE_METHOD                   1  /* 47.88% faster than 4 */

/* Tuneup completed successfully, took 29014 seconds */

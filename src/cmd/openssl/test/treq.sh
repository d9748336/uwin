#!/bin/sh


if [ x"$1" != "x" ]; then
	CNF=$1
else
	CNF=../apps/openssl.cnf
fi
if [ x"$2" != "x" ]; then
	t=$2
else
	t=testreq.pem
fi

cmd="openssl req -config $CNF"

if $cmd -in $t -inform p -noout -text 2>&1 | fgrep -i 'Unknown Public Key'; then
  echo "skipping req conversion test for $t"
  exit 0
fi

echo testing req conversions
cp $t fff.p

echo "p -> d"
$cmd -in fff.p -inform p -outform d >f.d
if [ $? != 0 ]; then exit 1; fi
#echo "p -> t"
#$cmd -in fff.p -inform p -outform t >f.t
#if [ $? != 0 ]; then exit 1; fi
echo "p -> p"
$cmd -in fff.p -inform p -outform p >f.p
if [ $? != 0 ]; then exit 1; fi

echo "d -> d"
$cmd -verify -in f.d -inform d -outform d >ff.d1
if [ $? != 0 ]; then exit 1; fi
#echo "t -> d"
#$cmd -in f.t -inform t -outform d >ff.d2
#if [ $? != 0 ]; then exit 1; fi
echo "p -> d"
$cmd -verify -in f.p -inform p -outform d >ff.d3
if [ $? != 0 ]; then exit 1; fi

#echo "d -> t"
#$cmd -in f.d -inform d -outform t >ff.t1
#if [ $? != 0 ]; then exit 1; fi
#echo "t -> t"
#$cmd -in f.t -inform t -outform t >ff.t2
#if [ $? != 0 ]; then exit 1; fi
#echo "p -> t"
#$cmd -in f.p -inform p -outform t >ff.t3
#if [ $? != 0 ]; then exit 1; fi

echo "d -> p"
$cmd -in f.d -inform d -outform p >ff.p1
if [ $? != 0 ]; then exit 1; fi
#echo "t -> p"
#$cmd -in f.t -inform t -outform p >ff.p2
#if [ $? != 0 ]; then exit 1; fi
echo "p -> p"
$cmd -in f.p -inform p -outform p >ff.p3
if [ $? != 0 ]; then exit 1; fi

cmp fff.p f.p
if [ $? != 0 ]; then exit 1; fi
cmp fff.p ff.p1
if [ $? != 0 ]; then exit 1; fi
#cmp fff.p ff.p2
#if [ $? != 0 ]; then exit 1; fi
cmp fff.p ff.p3
if [ $? != 0 ]; then exit 1; fi

#cmp f.t ff.t1
#if [ $? != 0 ]; then exit 1; fi
#cmp f.t ff.t2
#if [ $? != 0 ]; then exit 1; fi
#cmp f.t ff.t3
#if [ $? != 0 ]; then exit 1; fi

cmp f.p ff.p1
if [ $? != 0 ]; then exit 1; fi
#cmp f.p ff.p2
#if [ $? != 0 ]; then exit 1; fi
cmp f.p ff.p3
if [ $? != 0 ]; then exit 1; fi

/bin/rm -f f.* ff.* fff.*
exit 0
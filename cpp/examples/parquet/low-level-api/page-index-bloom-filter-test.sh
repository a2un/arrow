#$ARROW_HOME/build/debug/parquet-writer-with-pageindex  $1
rm -rf $ARROW_HOME/build/debug/tests/nonmember 
rm -rf $ARROW_HOME/build/debug/tests/member 

mkdir -p $ARROW_HOME/build/debug/tests/nonmember    
mkdir -p $ARROW_HOME/build/debug/tests/member 

cp parquet_cpp_example_$1_sorted.parquet $ARROW_HOME/build/debug/tests/member       
cp parquet_cpp_example_$1_sorted.parquet $ARROW_HOME/build/debug/tests/nonmember   
cp parquet_cpp_example_$1_unsorted.parquet $ARROW_HOME/build/debug/tests/member      
cp parquet_cpp_example_$1_unsorted.parquet $ARROW_HOME/build/debug/tests/nonmember 

## member queries
echo "Launching member queries.."
$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_sorted.parquet   $1/100 0 >> $ARROW_HOME/build/debug/tests/member/debug_m_10M_int32_s  &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_unsorted.parquet  $1/100 0 >> $ARROW_HOME/build/debug/tests/member/debug_m_10M_int32_u &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_sorted.parquet   $1/100 1 >> $ARROW_HOME/build/debug/tests/member/debug_m_10M_int64_s  &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_unsorted.parquet  $1/100 1 >> $ARROW_HOME/build/debug/tests/member/debug_m_10M_int64_u &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_sorted.parquet   $1/100 2 >> $ARROW_HOME/build/debug/tests/member/debug_m_10M_float_s  &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_unsorted.parquet  $1/100 2 >> $ARROW_HOME/build/debug/tests/member/debug_m_10M_float_u &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_sorted.parquet   $1/100 3 >> $ARROW_HOME/build/debug/tests/member/debug_m_10M_double_s  &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_unsorted.parquet  $1/100 3 >> $ARROW_HOME/build/debug/tests/member/debug_m_10M_double_u &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_sorted.parquet   $1/100 4 >> $ARROW_HOME/build/debug/tests/member/debug_m_10M_ByteArray_s  &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_unsorted.parquet  $1/100 4 >> $ARROW_HOME/build/debug/tests/member/debug_m_10M_ByteArray_u &



## non-member queries
echo "launching non-member queries.."
$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_sorted.parquet   $1 0 >> $ARROW_HOME/build/debug/tests/nonmember/debug_n_10M_int32_s  &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_unsorted.parquet  $1 0 >> $ARROW_HOME/build/debug/tests/nonmember/debug_n_10M_int32_u &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_sorted.parquet   $1 1 >> $ARROW_HOME/build/debug/tests/nonmember/debug_n_10M_int64_s  &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_unsorted.parquet  $1 1 >> $ARROW_HOME/build/debug/tests/nonmember/debug_n_10M_int64_u &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_sorted.parquet   $1 2 >> $ARROW_HOME/build/debug/tests/nonmember/debug_n_10M_float_s  &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_unsorted.parquet  $1 2 >> $ARROW_HOME/build/debug/tests/nonmember/debug_n_10M_float_u &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_sorted.parquet   $1 3 >> $ARROW_HOME/build/debug/tests/nonmember/debug_n_10M_double_s  &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_unsorted.parquet  $1 3 >> $ARROW_HOME/build/debug/tests/nonmember/debug_n_10M_double_u &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_sorted.parquet   $1 4 >> $ARROW_HOME/build/debug/tests/nonmember/debug_n_10M_ByteArray_s  &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_unsorted.parquet  $1 4 >> $ARROW_HOME/build/debug/tests/nonmember/debug_n_10M_ByteArray_u &

cd ..

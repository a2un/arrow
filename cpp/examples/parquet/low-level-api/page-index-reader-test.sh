## member queries
echo "Launching member queries.."
$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_int32_sorted.parquet  $3 $2 >> $ARROW_HOME/build/debug/tests/member/debug_m_10M_$2_s  &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_int32_unsorted.parquet $3 $2 >> $ARROW_HOME/build/debug/tests/member/debug_m_10M_$2_u &

## non-member queries
echo "launching non-member queries.."
$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_int32_sorted.parquet  $4 $2  >> $ARROW_HOME/build/debug/tests/nonmember/debug_n_10M_$2_s  &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_$1_int32_unsorted.parquet $4 $2 >> $ARROW_HOME/build/debug/tests/nonmember/debug_n_10M_$2_u &

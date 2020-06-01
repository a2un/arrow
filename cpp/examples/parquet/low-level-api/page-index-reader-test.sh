## member queries
echo "Launching member queries.."
$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_10000000_sorted.parquet  1000000 >> $ARROW_HOME/build/debug/tests/member/debug_m_10M_s  &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_10000000_unsorted.parquet 1000000 >> $ARROW_HOME/build/debug/tests/member/debug_m_10M_u &

## non-member queries
echo "launching non-member queries.."
$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_10000000_sorted.parquet  10000000  >> $ARROW_HOME/build/debug/tests/nonmember/debug_n_10M_s  &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ~/parquet_data/parquet_cpp_example_10000000_unsorted.parquet 10000000 >> $ARROW_HOME/build/debug/tests/nonmember/debug_n_10M_u &

#perf record -ag -e faults -p $pid

#iostat -k 1 -p sda > ~/parquet_data/debug_read_writes
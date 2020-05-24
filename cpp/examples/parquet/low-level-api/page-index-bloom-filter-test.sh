$ARROW_HOME/build/debug/parquet-writer-with-pageindex  10000000

## member queries
$ARROW_HOME/build/debug/parquet-reader-with-pageindex parquet_cpp_example_10000000_sorted.parquet   10000000  >> debug_m_10Ms  &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex parquet_cpp_example_10000000_unsorted.parquet  10000000  >> debug_m_10Mu &

## non-member queries
$ARROW_HOME/build/debug/parquet-reader-with-pageindex parquet_cpp_example_10000000_sorted.parquet   100000000  >> debug_n_10Ms &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex parquet_cpp_example_10000000_unsorted.parquet  100000000  >> debug_n_10Mu &
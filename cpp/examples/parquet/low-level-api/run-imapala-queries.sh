source $IMPALA_HOME/bin/impala-config.sh
$HADOOP_HOME/bin/hdfs dfs -rm -r -skipTrash /panos/import_dir/UNCOMPRESSED

$HADOOP_HOME/bin/hdfs dfs -mkdir -p /panos/import_dir/UNCOMPRESSED/20cols/1M
$HADOOP_HOME/bin/hdfs dfs -mkdir -p /panos/import_dir/UNCOMPRESSED/20cols/1pt25M
$HADOOP_HOME/bin/hdfs dfs -mkdir -p /panos/import_dir/UNCOMPRESSED/20cols/1pt5M
$HADOOP_HOME/bin/hdfs dfs -mkdir -p /panos/import_dir/UNCOMPRESSED/20cols/1pt75M
$HADOOP_HOME/bin/hdfs dfs -mkdir -p /panos/import_dir/UNCOMPRESSED/20cols/2M


$HADOOP_HOME/bin/hdfs dfs -copyFromLocal ~/parquet_data/testing/imports/UNCOMPRESSED/20cols/parquet_cpp_example_1M.parquet /panos/import_dir/UNCOMPRESSED/20cols/1M/all-types-1M.parquet

$HADOOP_HOME/bin/hdfs dfs -copyFromLocal ~/parquet_data/testing/imports/UNCOMPRESSED/20cols/parquet_cpp_example_1pt25M.parquet /panos/import_dir/UNCOMPRESSED/20cols/1pt25M/all-types-1pt25M.parquet

$HADOOP_HOME/bin/hdfs dfs -copyFromLocal ~/parquet_data/testing/imports/UNCOMPRESSED/20cols/parquet_cpp_example_1pt5M.parquet /panos/import_dir/UNCOMPRESSED/20cols/1pt5M/all-types-1pt5M.parquet

$HADOOP_HOME/bin/hdfs dfs -copyFromLocal ~/parquet_data/testing/imports/UNCOMPRESSED/20cols/parquet_cpp_example_1pt75M.parquet /panos/import_dir/UNCOMPRESSED/20cols/1pt75M/all-types-1pt75M.parquet

$HADOOP_HOME/bin/hdfs dfs -copyFromLocal ~/parquet_data/testing/imports/UNCOMPRESSED/20cols/parquet_cpp_example_2M.parquet /panos/import_dir/UNCOMPRESSED/20cols/2M/all-types-2M.parquet

$HADOOP_HOME/bin/hdfs dfs -ls -R /panos/import_dir/UNCOMPRESSED


$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE all_types_table_u_1M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE all_types_table_u_1pt25M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE all_types_table_u_1pt5M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE all_types_table_u_1pt75M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE all_types_table_u_2M";

$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;CREATE EXTERNAL TABLE all_types_table_u_1M LIKE PARQUET '/panos/import_dir/UNCOMPRESSED/20cols/1M/all-types-1M.parquet' STORED AS PARQUET LOCATION '/panos/import_dir/UNCOMPRESSED/20cols/1M';"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;CREATE EXTERNAL TABLE all_types_table_u_1pt25M LIKE PARQUET '/panos/import_dir/UNCOMPRESSED/20cols/1pt25M/all-types-1pt25M.parquet' STORED AS PARQUET LOCATION '/panos/import_dir/UNCOMPRESSED/20cols/1pt25M';"

$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;CREATE EXTERNAL TABLE all_types_table_u_1pt5M LIKE PARQUET '/panos/import_dir/UNCOMPRESSED/20cols/1pt5M/all-types-1pt5M.parquet' STORED AS PARQUET LOCATION '/panos/import_dir/UNCOMPRESSED/20cols/1pt5M';"

$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;CREATE EXTERNAL TABLE all_types_table_u_1pt75M LIKE PARQUET '/panos/import_dir/UNCOMPRESSED/20cols/1pt75M/all-types-1pt75M.parquet' STORED AS PARQUET LOCATION '/panos/import_dir/UNCOMPRESSED/20cols/1pt75M';"

$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;CREATE EXTERNAL TABLE all_types_table_u_2M LIKE PARQUET '/panos/import_dir/UNCOMPRESSED/20cols/2M/all-types-2M.parquet' STORED AS PARQUET LOCATION '/panos/import_dir/UNCOMPRESSED/20cols/2M';"


$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from all_types_table_u_1M;"
$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from all_types_table_u_1pt25M;"
$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from all_types_table_u_1pt5M;"
$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from all_types_table_u_1pt75M;"
$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from all_types_table_u_2M;"

$HADOOP_HOME/bin/hdfs dfs -rm -r -skipTrash /panos/export_dir/UNCOMPRESSED/
$HADOOP_HOME/bin/hdfs dfs -mkdir -p /panos/export_dir/UNCOMPRESSED/

$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE export_all_types_table_u_1M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE export_all_types_table_u_1pt25M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE export_all_types_table_u_1pt5M";

$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE export_all_types_table_u_1pt75M";

$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE export_all_types_table_u_2M";


$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;create table export_all_types_table_u_1M LIKE all_types_table_u_1M STORED AS PARQUET LOCATION '/panos/export_dir/UNCOMPRESSED/20cols/1M';"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;create table export_all_types_table_u_1pt25M LIKE all_types_table_u_1pt25M STORED AS PARQUET LOCATION '/panos/export_dir/UNCOMPRESSED/20cols/1pt25M';"

$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;create table export_all_types_table_u_1pt5M LIKE all_types_table_u_1pt5M STORED AS PARQUET LOCATION '/panos/export_dir/UNCOMPRESSED/20cols/1pt5M';"

$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;create table export_all_types_table_u_1pt75M LIKE all_types_table_u_1pt75M STORED AS PARQUET LOCATION '/panos/export_dir/UNCOMPRESSED/20cols/1pt75M';"

$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;create table export_all_types_table_u_2M LIKE all_types_table_u_2M STORED AS PARQUET LOCATION '/panos/export_dir/UNCOMPRESSED/20cols/2M';"

$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;INSERT INTO TABLE  export_all_types_table_u_1M SELECT * FROM all_types_table_u_1M;"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;INSERT INTO TABLE  export_all_types_table_u_1pt25M SELECT * FROM all_types_table_u_1pt25M;"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;INSERT INTO TABLE  export_all_types_table_u_1pt5M SELECT * FROM all_types_table_u_1pt5M;"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;INSERT INTO TABLE  export_all_types_table_u_1pt75M SELECT * FROM all_types_table_u_1pt75M;"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;INSERT INTO TABLE  export_all_types_table_u_2M SELECT * FROM all_types_table_u_2M;"

$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from export_all_types_table_u_1M;"
$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from export_all_types_table_u_1pt25M;"
$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from export_all_types_table_u_1pt5M;"
$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from export_all_types_table_u_1pt75M;"
$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from export_all_types_table_u_2M;"

$HADOOP_HOME/bin/hdfs dfs -ls -R /panos/export_dir/UNCOMPRESSED

rm -rf ~/parquet_data/testing/exports/UNCOMPRESSED/20cols/

mkdir -p ~/parquet_data/testing/exports/UNCOMPRESSED/20cols/

hadoop fs -get /panos/export_dir/UNCOMPRESSED/*   ~/parquet_data/testing/exports/UNCOMPRESSED/
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE all_types_table_u_1M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE all_types_table_u_1pt25M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE all_types_table_u_1pt5M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE all_types_table_u_1pt75M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE all_types_table_u_2M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE export_all_types_table_u_1M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE export_all_types_table_u_1pt25M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE export_all_types_table_u_1pt5M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE export_all_types_table_u_1pt75M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE export_all_types_table_u_2M";

$HADOOP_HOME/bin/hdfs dfs -rm -r -skipTrash /panos/import_dir/UNCOMPRESSED
$HADOOP_HOME/bin/hdfs dfs -rm -r -skipTrash /panos/export_dir/UNCOMPRESSED

ls -laR ~/parquet_data/testing/exports/UNCOMPRESSED/

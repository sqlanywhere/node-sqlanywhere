These examples are intended to be used with the SQL Anywhere node.js driver.

Note that you will need to:
* have installed the SQLAnywhere client on your machine before attempting to run these tests.
* be persistent: the SQLAnywhere test instance we have in Kidozen is very unstable and might not respond after a couple of retrials.
* you can always test the connection to the DB using the following command from the proper binxx subdirectory of your
SQLAnywhere installation dir:

dbping -d -c "Host=54.147.208.71:2638;UserId=DBA;Password=sql"
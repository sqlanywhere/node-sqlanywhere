// Integration tests. They need a running SQL Anywhere server, for example:
//
//   dbsrv17 -n node_test -x "tcpip(port=2638)" demo.db
//   SQLANY_TEST_CONN="Host=localhost:2638;ServerName=node_test;UID=DBA;PWD=sql" npm test
//
// DBCAPI must be loadable: source sa_config.sh, or point SQLANY_API_DLL at
// libdbcapi_r.so / libdbcapi_r.dylib.
'use strict';

const { describe, test, before, after } = require('node:test');
const assert = require('node:assert');
const { promisify } = require('node:util');
const { spawnSync } = require('node:child_process');
const path = require('node:path');
const sa = require('..');

const CONN = process.env.SQLANY_TEST_CONN;
const opts = { skip: CONN ? false : 'set SQLANY_TEST_CONN to run' };

// Runs script in a separate node process with the driver as `sa`
function runScript(script, flags = []) {
    const prelude = `const sa = require(${JSON.stringify(path.join(__dirname, '..'))});\n` +
	`const CONN = ${JSON.stringify(CONN)};\n`;
    return spawnSync(process.execPath, [...flags, '-e', prelude + script], { encoding: 'utf8' });
}

describe('synchronous calls', opts, () => {
    let conn;

    before(() => {
	conn = sa.createConnection(CONN);
	conn.connect();
    });

    after(() => conn.disconnect());

    test('connect and disconnect', () => {
	const c = sa.createConnection();
	assert.strictEqual(c.connected(), false);
	c.connect(CONN);
	assert.strictEqual(c.connected(), true);
	assert.throws(() => c.connect(CONN), /^Error: Code: -2003 Msg: Already Connected$/);
	c.close();
	assert.strictEqual(c.connected(), false);
	c.disconnect(); // not an error when already disconnected
    });

    test('connection parameters are combined', () => {
	const params = Object.fromEntries(CONN.split(';').map(p => p.split('=')));
	const c = sa.createConnection({ CharSet: 'UTF-8' });
	c.connect(params);
	assert.deepStrictEqual(c.exec('SELECT 1 AS x'), [{ x: 1 }]);
	c.disconnect();
	assert.throws(() => sa.createConnection().connect(CONN + ';PWD=wrong-password'),
		      /^Error: Code: -103 Msg: /);
    });

    test('column types', () => {
	const rows = conn.exec(`SELECT CAST(-1 AS BIGINT) a, CAST(2 AS UNSIGNED BIGINT) b,
	    CAST(-3 AS SMALLINT) c, CAST(4 AS TINYINT) d, CAST(1 AS BIT) e, CAST(1.25 AS REAL) f,
	    CAST(12.345 AS NUMERIC(10,3)) g, DATE('2024-01-02') h,
	    CAST('2024-01-02 03:04:05.123' AS TIMESTAMP) i, CAST(4000000000 AS UNSIGNED INT) j,
	    CAST(0x00ff10 AS BINARY(3)) k, CAST('ünïcödé ✓' AS NVARCHAR(20)) l, '' m, CAST(NULL AS INT) n`);
	assert.deepStrictEqual(rows, [{
	    a: -1, b: 2, c: -3, d: 4, e: 1, f: 1.25, g: '12.345', h: '2024-01-02',
	    i: '2024-01-02 03:04:05.123', j: 4000000000, k: Buffer.from([0, 0xff, 0x10]),
	    l: 'ünïcödé ✓', m: '', n: null,
	}]);
    });

    test('rows that only hold NULL are returned', () => {
	assert.deepStrictEqual(conn.exec('SELECT NULL AS a, NULL AS b'), [{ a: null, b: null }]);
	assert.deepStrictEqual(
	    conn.exec('SELECT CASE WHEN row_num = 2 THEN 1 END AS a FROM sa_rowgenerator(1, 3) ORDER BY row_num'),
	    [{ a: null }, { a: 1 }, { a: null }]);
    });

    test('large result set', () => {
	const rows = conn.exec('SELECT row_num, CAST(row_num AS VARCHAR(10)) s FROM sa_rowgenerator(1, 50000) ORDER BY row_num');
	assert.strictEqual(rows.length, 50000);
	assert.deepStrictEqual(rows[49999], { row_num: 50000, s: '50000' });
    });

    test('bind parameters', () => {
	const rows = conn.exec(`SELECT CAST(? AS INT) i, CAST(? AS DOUBLE) d, CAST(? AS DOUBLE) big,
	    CAST(? AS LONG NVARCHAR) s, CAST(? AS LONG BINARY) b, CAST(? AS VARCHAR(10)) n,
	    CAST(? AS LONG BINARY) empty_b, CAST(? AS VARCHAR(5)) empty_s`,
	    [42, 1.5, 3000000000, 'привет', Buffer.from([1, 2, 3]), null, Buffer.alloc(0), '']);
	assert.deepStrictEqual(rows, [{
	    i: 42, d: 1.5, big: 3000000000, s: 'привет', b: Buffer.from([1, 2, 3]), n: null,
	    empty_b: Buffer.alloc(0), empty_s: '',
	}]);
	assert.deepStrictEqual(conn.exec('SELECT 1 AS x', []), [{ x: 1 }]);
    });

    test('unsupported bind values', () => {
	for (const value of [true, undefined, {}, () => 1]) {
	    assert.throws(() => conn.exec('SELECT ? AS x', [value]),
			  /^Error: Code: -2006 Msg: Can not bind parameter\(s\)$/);
	}
    });

    test('insert, update and delete', () => {
	conn.exec('CREATE TABLE #dml (id INT PRIMARY KEY, s VARCHAR(20) NULL, b LONG BINARY NULL)');
	assert.strictEqual(conn.exec('INSERT INTO #dml VALUES (?, ?, ?)', [1, 'one', Buffer.from('1')]), 1);
	assert.strictEqual(
	    conn.exec('INSERT INTO #dml VALUES (?, ?, ?)',
		      [[2, 'two', Buffer.from('2')], [3, 'three', Buffer.alloc(0)], [4, '', Buffer.from('4')]]),
	    3);
	assert.strictEqual(conn.exec('INSERT INTO #dml (id, s) VALUES (?, ?)', [[5, null], [6, null]]), 2);
	assert.throws(() => conn.exec('INSERT INTO #dml (id, s) VALUES (?, ?)', [[7, 'x'], [8, 9]]),
		      /Code: -2006/);
	assert.strictEqual(conn.exec('UPDATE #dml SET s = ? WHERE id > ?', ['x', 3]), 3);
	// No affected rows gives undefined, as in earlier versions
	assert.strictEqual(conn.exec('UPDATE #dml SET s = ? WHERE id > ?', ['x', 100]), undefined);
	assert.strictEqual(conn.exec('DELETE FROM #dml WHERE id = 6'), 1);
	assert.deepStrictEqual(conn.exec('SELECT id, s FROM #dml WHERE id <= 3 ORDER BY id'),
			       [{ id: 1, s: 'one' }, { id: 2, s: 'two' }, { id: 3, s: 'three' }]);
	assert.throws(() => conn.exec('INSERT INTO #dml (id) VALUES (1)'), /^Error: Code: -193 Msg: /);
    });

    test('wide statements with NULL in some rows', () => {
	conn.exec('CREATE TABLE #wn (id INT PRIMARY KEY, d DOUBLE NULL, s VARCHAR(20) NULL, b LONG BINARY NULL)');
	const toRow = ([id, d, s, b]) => ({ id, d, s, b });
	const rows = [
	    [1, 1.5, 'a', Buffer.from('a')], [2, null, 'b', null], [3, null, null, null],
	    [4, 2.5, null, Buffer.from('d')], [5, 3.5, '', Buffer.alloc(0)]];
	assert.strictEqual(conn.exec('INSERT INTO #wn VALUES (?, ?, ?, ?)', rows), 5);
	assert.deepStrictEqual(conn.exec('SELECT * FROM #wn ORDER BY id'), rows.map(toRow));

	const ins = conn.prepare('INSERT INTO #wn VALUES (?, ?, ?, ?)');
	const more = rows.map(([id, ...values]) => [id + 10, ...values]);
	assert.strictEqual(ins.exec(more), 5);
	assert.strictEqual(ins.exec([[21, null, 'x', null], [22, 1, null, null]]), 2);
	ins.drop();
	assert.deepStrictEqual(conn.exec('SELECT * FROM #wn WHERE id > 10 ORDER BY id'),
			       [...more, [21, null, 'x', null], [22, 1, null, null]].map(toRow));

	// A failing row undoes the rows before it, as a failing multi-row
	// execution does
	assert.throws(() => conn.exec('INSERT INTO #wn (id, s) VALUES (?, ?)', [[30, 'x'], [31, null], [1, 'y']]),
		      /^Error: Code: -193 Msg: /);
	assert.deepStrictEqual(conn.exec('SELECT id FROM #wn WHERE id >= 30'), []);

	assert.strictEqual(conn.exec('UPDATE #wn SET s = ? WHERE id = ?', [[null, 1], ['z', 2]]), 2);
	assert.deepStrictEqual(conn.exec('SELECT id, s FROM #wn WHERE id <= 2 ORDER BY id'),
			       [{ id: 1, s: null }, { id: 2, s: 'z' }]);
	assert.throws(() => conn.exec('SELECT ? AS x', [[1], [null]]),
		      /^Error: Code: -1 Msg: Cannot bind parameters to arrays /);
    });

    test('prepared statements', () => {
	conn.exec('CREATE TABLE #prep (id INT PRIMARY KEY, s VARCHAR(20))');
	const ins = conn.prepare('INSERT INTO #prep VALUES (?, ?)');
	assert.strictEqual(ins.exec([[1, 'a'], [2, 'b']]), 2);
	// as many rows again, and a single row in between, which must not
	// reuse the batch size
	assert.strictEqual(ins.exec([[3, 'c'], [4, 'd']]), 2);
	assert.strictEqual(ins.exec([5, 'e']), 1);
	assert.strictEqual(ins.exec([[6, 'f'], [7, 'g'], [8, 'h']]), 3);
	assert.strictEqual(ins.exec([[9, 'i'], [10, 'j']]), 2);
	// a failing execution inserts nothing and leaves the statement usable
	assert.throws(() => ins.exec([[11, 'k'], [1, 'x']]), /^Error: Code: -193 Msg: /);
	assert.strictEqual(ins.exec([[11, 'k'], [12, 'l']]), 2);
	// values missing from the rows are NULL, as with conn.exec(), rather
	// than the values of an earlier execution
	assert.strictEqual(ins.exec([[13], [14]]), 2);
	ins.drop();
	assert.deepStrictEqual(conn.exec('SELECT id, s FROM #prep ORDER BY id'),
			       [...'abcdefghijkl', null, null].map((s, i) => ({ id: i + 1, s })));

	const sel = conn.prepare('SELECT id, s FROM #prep WHERE id >= ? ORDER BY id');
	assert.deepStrictEqual(sel.exec([12]), [{ id: 12, s: 'l' }, { id: 13, s: null }, { id: 14, s: null }]);
	assert.deepStrictEqual(sel.exec([14]), [{ id: 14, s: null }]);
	// DBCAPI rejects several rows for a query, which leaves the statement
	// usable for one row, although it is not reset
	assert.throws(() => sel.exec([[1], [2]]), /^Error: Code: -1 Msg: Cannot bind parameters to arrays /);
	assert.deepStrictEqual(sel.exec([14]), [{ id: 14, s: null }]);
	assert.deepStrictEqual(sel.exec(), []); // id >= NULL
	sel.drop();
	assert.throws(() => sel.exec([1]), /^Error: Code: -2001 Msg: Invalid Object$/);
	sel.drop(); // dropping twice is harmless

	assert.throws(() => conn.prepare('SELEC 1'), /^Error: Code: -265 Msg: /);
    });

    test('missing values are bound as NULL', () => {
	// The server takes a parameter left unbound as NULL, but then kept it
	// NULL in later executions on the connection, also of other INSERT
	// statements into the table, and the fourth execution crashed it
	conn.exec('CREATE TABLE #miss (id INT PRIMARY KEY, s VARCHAR(20) NULL, d DOUBLE NULL)');
	const ins = conn.prepare('INSERT INTO #miss VALUES (?, ?, ?)');
	assert.strictEqual(ins.exec([1]), 1);
	assert.strictEqual(ins.exec([2, 'b', 2.5]), 1);
	assert.strictEqual(ins.exec([[3], [4]]), 2);
	assert.strictEqual(ins.exec([[5, 'e', 5.5], [6, 'f', 6.5], [7, 'g', 7.5]]), 3);
	// executed row by row, as s mixes NULL and other values
	assert.strictEqual(ins.exec([[8, null], [9, 'i']]), 2);
	ins.drop();
	assert.strictEqual(conn.exec('INSERT INTO #miss VALUES (?, ?, ?)', [10, 'j']), 1);
	assert.strictEqual(conn.exec('INSERT INTO #miss (d, s, id) VALUES (?, ?, ?)', [11.5, 'k', 11]), 1);
	assert.strictEqual(conn.exec('INSERT INTO #miss (id, s) VALUES (12, ?)'), 1);
	assert.strictEqual(conn.exec('INSERT INTO #miss (id, s) VALUES (13, ?)', ['m']), 1);
	const rows = [[1], [2, 'b', 2.5], [3], [4], [5, 'e', 5.5], [6, 'f', 6.5], [7, 'g', 7.5],
		      [8], [9, 'i'], [10, 'j'], [11, 'k', 11.5], [12], [13, 'm']];
	assert.deepStrictEqual(conn.exec('SELECT * FROM #miss ORDER BY id'),
			       rows.map(([id, s = null, d = null]) => ({ id, s, d })));

	assert.deepStrictEqual(conn.exec('SELECT CAST(? AS INT) AS a, CAST(? AS VARCHAR(5)) AS b', [1]),
			       [{ a: 1, b: null }]);
	assert.deepStrictEqual(conn.exec('SELECT CAST(? AS INT) AS a'), [{ a: null }]);
    });

    test('procedure output parameters', () => {
	// OUT parameters are never bound, so their values are ignored, and
	// INOUT ones only pass their value in. Statements used to be reset
	// before binding, after which DBCAPI took the IN parameters of these
	// calls for OUT ones: their values were lost, or the process crashed.
	conn.exec('CREATE TABLE #calls (id INT DEFAULT AUTOINCREMENT PRIMARY KEY, a INT NULL, s VARCHAR(40) NULL)');
	// Parameters must not be named like the columns: SQL Anywhere 16 then
	// fails the INSERT with -143 "Column 'a' not found"
	conn.exec(`CREATE OR REPLACE PROCEDURE node_test_out(IN p_a INT, OUT b VARCHAR(20))
	    BEGIN INSERT INTO #calls (a, s) VALUES (p_a, 'out'); SET b = STRING(p_a * 2) END`);
	conn.exec(`CREATE OR REPLACE PROCEDURE node_test_out_first(OUT b INT, IN p_a INT)
	    BEGIN INSERT INTO #calls (a, s) VALUES (p_a, 'out first'); SET b = p_a END`);
	conn.exec(`CREATE OR REPLACE PROCEDURE node_test_inout(IN p_a INT, INOUT p_c VARCHAR(40), INOUT p_d INT)
	    BEGIN INSERT INTO #calls (a, s) VALUES (p_a, STRING(ISNULL(p_c, 'null'), '/', ISNULL(p_d, -1)));
	    SET p_c = STRING(p_c, ' and a longer output'); SET p_d = p_d + 1 END`);
	assert.strictEqual(conn.exec('CALL node_test_out(?, ?)', [1, 'x'.repeat(20)]), 1);
	assert.strictEqual(conn.exec('CALL node_test_out(?, ?)', [2, 5]), 1);
	assert.strictEqual(conn.exec('CALL node_test_out(?, ?)', [3, null]), 1);
	assert.strictEqual(conn.exec('CALL node_test_out(?, ?)', [4]), 1);
	assert.strictEqual(conn.exec('CALL node_test_out_first(?, ?)', [null, 5]), 1);
	// The output of c no longer fails as too long for the input value
	assert.strictEqual(conn.exec('CALL node_test_inout(?, ?, ?)', [6, 'c', 7]), 1);
	assert.strictEqual(conn.exec('CALL node_test_inout(?, ?, ?)', [7, null, 8]), 1);
	assert.strictEqual(conn.exec('CALL node_test_inout(?, ?, ?)', [8]), 1);

	const out = conn.prepare('CALL node_test_out(?, ?)');
	assert.strictEqual(out.exec([9, 'x']), 1);
	assert.strictEqual(out.exec([10]), 1);
	assert.strictEqual(out.exec([11, 'y']), 1);
	// A CALL can't be executed for several rows at once
	assert.throws(() => out.exec([[12], [13]]), /^Error: Code: -182 Msg: /);
	assert.throws(() => out.exec([[12, 'a'], [13, 'b']]), /^Error: Code: -182 Msg: /);
	assert.strictEqual(out.exec([14]), 1);
	out.drop();
	assert.throws(() => conn.exec('CALL node_test_out(?, ?)', [[15], [16]]), /^Error: Code: -182 Msg: /);
	assert.throws(() => conn.exec('CALL node_test_inout(?, ?, ?)', [[15, 'a', 1], [16, 'b', 2]]),
		      /^Error: Code: -182 Msg: /);

	assert.deepStrictEqual(conn.exec('SELECT a, s FROM #calls ORDER BY id'), [
	    { a: 1, s: 'out' }, { a: 2, s: 'out' }, { a: 3, s: 'out' }, { a: 4, s: 'out' },
	    { a: 5, s: 'out first' }, { a: 6, s: 'c/7' }, { a: 7, s: 'null/8' }, { a: 8, s: 'null/-1' },
	    { a: 9, s: 'out' }, { a: 10, s: 'out' }, { a: 11, s: 'out' }, { a: 14, s: 'out' }]);
	for (const proc of ['node_test_out', 'node_test_out_first', 'node_test_inout']) {
	    conn.exec(`DROP PROCEDURE ${proc}`);
	}
    });

    test('multiple result sets', () => {
	conn.exec('CREATE OR REPLACE PROCEDURE node_test_two_sets() BEGIN SELECT 1 AS a; SELECT 2 AS b, 3 AS c; END');
	const stmt = conn.prepare('CALL node_test_two_sets()');
	assert.deepStrictEqual(stmt.exec(), [{ a: 1 }]);
	assert.deepStrictEqual(stmt.getMoreResults(), [{ b: 2, c: 3 }]);
	assert.strictEqual(stmt.getMoreResults(), undefined);
	// executing again, also before all result sets are read
	assert.deepStrictEqual(stmt.exec(), [{ a: 1 }]);
	assert.deepStrictEqual(stmt.exec(), [{ a: 1 }]);
	assert.deepStrictEqual(stmt.getMoreResults(), [{ b: 2, c: 3 }]);
	stmt.drop();
	conn.exec('DROP PROCEDURE node_test_two_sets');
    });

    test('errors while fetching rows are reported', () => {
	assert.throws(() => conn.exec(`SELECT CAST(v AS INT) AS x FROM (
	    SELECT row_num, CASE WHEN row_num = 50 THEN 'x' ELSE '1' END AS v
	    FROM sa_rowgenerator(1, 100)) t ORDER BY row_num`), /^Error: Code: -157 Msg: /);
    });

    test('commit and rollback', () => {
	conn.exec('CREATE TABLE #tx (id INT)');
	conn.exec('INSERT INTO #tx VALUES (1)');
	conn.commit();
	conn.exec('INSERT INTO #tx VALUES (2)');
	conn.rollback();
	assert.deepStrictEqual(conn.exec('SELECT id FROM #tx'), [{ id: 1 }]);
    });

    test('invalid arguments', () => {
	const invalid = /^Error: Code: -2002 Msg: Invalid Arguments$/;
	assert.throws(() => sa.createConnection(5), invalid);
	assert.throws(() => conn.exec(), invalid);
	assert.throws(() => conn.exec(1), invalid);
	assert.throws(() => conn.exec('SELECT 1', 5), invalid);
	assert.throws(() => conn.prepare(), invalid);
	assert.throws(() => conn.commit(1), invalid);
	assert.throws(() => conn.connect(1, 2), invalid);
	const stmt = conn.prepare('SELECT 1 AS x');
	assert.throws(() => stmt.exec(1), invalid);
	assert.throws(() => stmt.getMoreResults(1), invalid);
	stmt.drop();
    });

    test('calls on a closed connection', () => {
	const c = sa.createConnection(CONN);
	assert.throws(() => c.exec('SELECT 1'), /^Error: Code: -2001 Msg: Invalid Object$/);
	assert.throws(() => c.prepare('SELECT 1'), /^Error: Code: -2005 Msg: No Connection Available$/);
	assert.throws(() => c.commit(), /^Error: Code: -2005 Msg: No Connection Available$/);
	c.connect();
	const stmt = c.prepare('SELECT 1 AS x');
	c.disconnect();
	assert.throws(() => stmt.exec(), /^Error: Code: -2001 Msg: Invalid Object$/);
    });
});

describe('callbacks', opts, () => {
    test('connect, query and disconnect', async () => {
	const c = sa.createConnection(CONN);
	await promisify(c.connect.bind(c))();
	assert.deepStrictEqual(await promisify(c.exec.bind(c))('SELECT CAST(? AS INT) AS x', [7]),
			       [{ x: 7 }]);

	const stmt = await promisify(c.prepare.bind(c))('SELECT CAST(? AS VARCHAR(5)) AS x');
	assert.deepStrictEqual(await promisify(stmt.exec.bind(stmt))(['hi']), [{ x: 'hi' }]);
	await promisify(stmt.drop.bind(stmt))();

	await promisify(c.commit.bind(c))();
	await promisify(c.rollback.bind(c))();
	await promisify(c.disconnect.bind(c))();
	assert.strictEqual(c.connected(), false);
    });

    test('callbacks get (err, result)', async () => {
	const c = sa.createConnection(CONN);
	const call = (fn, ...args) => new Promise(resolve => fn.call(c, ...args, (...cbArgs) => resolve(cbArgs)));

	let [err, result] = await call(c.exec, 'SELECT 1');
	assert.match(err.message, /^Code: -2001 Msg: Invalid Object$/);
	assert.strictEqual(result, undefined);

	assert.deepStrictEqual(await call(c.connect), [undefined, undefined]);
	assert.deepStrictEqual(await call(c.exec, 'SELECT 1 AS x'), [undefined, [{ x: 1 }]]);
	[err] = await call(c.exec, 'SELEC 1');
	assert.match(err.message, /^Code: -265 Msg: /);
	[err] = await call(c.exec, 'SELECT ? AS x', [true]);
	assert.match(err.message, /^Code: -2006 Msg: /);
	[err] = await call(c.connect);
	assert.match(err.message, /^Code: -2003 Msg: /);
	assert.deepStrictEqual(await call(c.disconnect), [undefined, undefined]);
	[err] = await call(c.commit);
	assert.match(err.message, /^Code: -2005 Msg: /);
    });

    test('concurrent queries on one connection', async () => {
	const c = sa.createConnection(CONN);
	c.connect();
	const exec = promisify(c.exec.bind(c));
	const results = await Promise.all(Array.from({ length: 50 }, (_, i) =>
	    exec('SELECT CAST(? AS INT) AS i, COUNT(*) AS n FROM sa_rowgenerator(1, ?)', [i, i + 1])));
	results.forEach((rows, i) => assert.deepStrictEqual(rows, [{ i, n: i + 1 }]));
	c.disconnect();
    });

    test('parallel connections', async () => {
	const conns = Array.from({ length: 5 }, () => sa.createConnection(CONN));
	await Promise.all(conns.map(c => promisify(c.connect.bind(c))()));
	const rows = await Promise.all(conns.map((c, i) =>
	    promisify(c.exec.bind(c))('SELECT CAST(? AS INT) AS i', [i])));
	assert.deepStrictEqual(rows, conns.map((_, i) => [{ i }]));
	await Promise.all(conns.map(c => promisify(c.disconnect.bind(c))()));
    });
});

describe('object lifetime', opts, () => {
    test('exceptions thrown by callbacks are uncaught exceptions', () => {
	const res = runScript(`
	    const c = sa.createConnection(CONN);
	    c.connect();
	    process.on('uncaughtException', err => {
		console.log('caught', err.message);
		c.disconnect();
	    });
	    c.exec('SELECT 1', () => { throw new Error('boom'); });
	`);
	assert.strictEqual(res.status, 0, res.stderr);
	assert.strictEqual(res.stdout, 'caught boom\n');
    });

    test('garbage collected connections and statements', () => {
	const res = runScript(`
	    const tick = () => new Promise(resolve => setImmediate(resolve));
	    (async () => {
		// connection collected while a statement still refers to it
		let c = sa.createConnection(CONN);
		c.connect();
		const stmt = c.prepare('SELECT 1 AS x');
		c = null;
		for (let i = 0; i < 5; i++) { gc(); await tick(); }
		try { stmt.exec(); } catch (err) { console.log(err.message); }
		stmt.drop();

		// objects only referenced by pending callbacks
		await new Promise((resolve, reject) => {
		    const c2 = sa.createConnection(CONN);
		    c2.connect();
		    c2.prepare('SELECT 2 AS x').exec((err, rows) => {
			if (err) return reject(err);
			console.log(JSON.stringify(rows));
			resolve();
		    });
		    gc();
		});

		// statements collected before their connection free their server
		// resources: the server allows 50 prepared statements per connection
		const c3 = sa.createConnection(CONN);
		c3.connect();
		for (let round = 0; round < 3; round++) {
		    for (let i = 0; i < 40; i++) c3.prepare('SELECT 1');
		    for (let i = 0; i < 5; i++) { gc(); await tick(); }
		}
		console.log(JSON.stringify(c3.exec('SELECT 3 AS x')));
		c3.disconnect();
	    })().catch(err => { console.error(err); process.exit(1); });
	`, ['--expose-gc']);
	assert.strictEqual(res.status, 0, res.stderr);
	assert.strictEqual(res.stdout, 'Code: -2001 Msg: Invalid Object\n[{"x":2}]\n[{"x":3}]\n');
    });
});

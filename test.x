def puts = $foreign("puts", func(s ^u8));

/*
def foo = func {
	let my_string =
		\ This is my line
		\ this is a second line
		\ etc.
		;
	puts(my_string);
}
*/

/* def add = func(dst *vec($N), v, u vec($N))
 * 	$poke(operator.addeq)
 * {
 * 	loop (i: 0...N)
 * 		dst[i] = v[i] + u[i];
 * }; */

def main′ = func {
	puts("Hello, sailor!");
	some_func(#b10.1100'1001e+11);
	slices_sort(my_slice, func(x, y int) int {
		return x - y;
	});
};

def some_func = func(n u32) u32 { return n * 2; };

/* def MY_FLOAT = union { f f64; n u64; } { n = 0x482DEF }.f */

def main = func { main′(); };

abstract record fallible<T>

final record success<T> extends fallible<T> {
	readonly T result;
}

abstract record error<T> extends fallible<T> {
	readonly array<char> msg;
}
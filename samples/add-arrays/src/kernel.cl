kernel void add_arrays(global int *c, global int *a, global int *b) {
  int i = get_global_id(0);
	c[i] = a[i] + b[i];
}


unsigned U_strlen(const char *p)
{
	unsigned len;

	for (len = 0; p[len]; len++)
	{}

	return len;
}

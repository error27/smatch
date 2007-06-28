/*
 * We get this one wrong too.
 *
 * It should result in a sequence
 *
 *	B ( )
 *	A ( )
 *	B ( )
 *	A ( )
 *
 * because each iteration of the scanning of "SCAN()"
 * should re-evaluate the recursive B->A->B expansion.
 * But we never re-evaluate something that we noticed
 * was recursive. So we will cause it to evaluate to
 *
 *	B ( )
 *	A ( )
 *	A ( )
 *	A ( )
 *
 * Which is really quite wrong.
 *
 * Did I already mention that the C preprocessor language
 * is a perverse thing?
 */

#define LP (

#define A() B LP )
#define B() A LP )

#define SCAN(x) x

A()                     // B ( )
SCAN( A() )             // A ( )
SCAN(SCAN( A() ))       // B ( )
SCAN(SCAN(SCAN( A() ))) // A ( )

struct sk_buff;
struct sock;

extern int skb_append_datato_frags(struct sock *sk, struct sk_buff *skb,
                    int getfrag(void *from, char *to, int offset,
                    int len,int odd, struct sk_buff *skb),
                    void *from, int length);

int skb_append_datato_frags(struct sock *sk, struct sk_buff *skb,
                    int (*getfrag)(void *from, char *to, int offset,
                    int len,int odd, struct sk_buff *skb),
                    void *from, int length)
{
    return 0;
}

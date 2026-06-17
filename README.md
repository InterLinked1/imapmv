# imapmv

*imapmv* is a simple utility optimized for moving messages between IMAP servers (in particular, from a remote server to a local server)

It is very flexible and allows messages matching any IMAP search query to be moved between folders of your choice. It is also very bandwidth-efficient as no extraneous IMAP operations are performed.

## How does this compare to similar tools?

There are a number of more general purpose tools already that can be used to move or copy messages between servers. I was not happy with the existing tools because they tended to have some major limitations:

* Folders on one server had to be 1:1 mapped to folders on the other server
* Lack of bandwidth efficiency, e.g. by running the `LIST` command before performing other operations. If the folder mapping is defined ahead of time, the `LIST` command is unnecessary.

My own personal use case was archiving IMAP messages from an Internet-accessible IMAP server to a private IMAP server on a regular basis, and doing so on a slow connection (33.6k dial-up link). Other tools I found to be unsuitable for the above reasons, so I wrote a tool optimized for this use case. If that is also your use case, this is probably the tool for you! If you have a different use case than this, you are probably better served by a more general tool like [imapsync](https://github.com/imapsync/imapsync).

## Building

*imapmv* must be compiled from source, which requires only a few simple steps.

*imapmv* requires the `libetpan` library, which can be installed using this script: https://github.com/InterLinked1/lbbs/blob/master/scripts/libetpan.sh

After you have that installed, you can simply run `make` and then `make install`.

## Configuration

**imapmv** requires two configuration files, one with the server connection configuration and one with a list of "move rules".

A typical server config will look like this:

`config.txt`:

```
host1=192.0.2.1
port1=993
secure1=yes
user1=user
pass1=password
host2=10.1.1.2
port2=143
secure2=no
user2=user
pass2=password
```

`rules.txt` (format `folder1`,`folder2`,`IMAP search query`)

```
INBOX,Archive.sender1,UNDELETED SEEN OLDER 259200 FROM sender1@example.com
INBOX,Archive.sender2,UNDELETED SEEN OLDER 259200 FROM sender2@example.com
INBOX,Archive.john,UNDELETED SEEN OLDER 259200 FROM john@example.com
```

Then you can run something like `imapmv -C config.txt -R rules.txt`. Run `imapmv -h` to see the full list of options available, e.g. to control expunge behavior, do a dry run, etc.

The above rules move any read messages older than 3 days old from the respective senders into the corresponding folder on the second server. Usually you will want to include `UNDELETED` at minimum in each rule so that messages with the `\Deleted` flag are not processed.

For more documentation about search keys you can use in a search string, see: https://www.marshallsoft.com/ImapSearch.htm

Rules are processed in the order they are specified in the file.

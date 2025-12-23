export default function handler(req, res) {
  if (req.method !== 'POST')
    return res.status(405).json({ message: 'Method not allowed' });

  const { username, password } = req.body;

  if (
    username === process.env.DASH_USER &&
    password === process.env.DASH_PASS
  ) {
    return res.json({ success: true });
  }

  res.status(401).json({ success: false });
}

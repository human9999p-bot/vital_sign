import { neon } from "@neondatabase/serverless";
const sql = neon(process.env.DATABASE_URL);

export default async function handler(req, res) {
  try {
    const rows = await sql`
      SELECT id,device_id,spo2,heartrate,time
      FROM sensor_data
      ORDER BY time DESC
    `;
    res.json(rows.reverse());
  } catch (e) {
    res.status(500).json({ error: e.message });
  }
}
